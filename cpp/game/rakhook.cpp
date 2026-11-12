#include "rakhook.h"
#include "game/netinfo.h"
#include "game/rakhook.h"
#include "hook/monethook.h"
#include "logger.h"
#include "raknet/BitStream.h"
#include "raknet/RakClientInterface.h"
#include "raknet/RakNetTypes.h"
#include "raknet/StringCompressor.h"
#include "script/script_manager.h"
#include <cstdlib>
#include <deque>

namespace {
RakClientInterface* rci;
bool inited;
bool emulation_only;
std::deque<RakNet::Packet*> emulation_queue;

struct queued_send {
  bool is_rpc;
  int rpc_id;
  RakNet::BitStream bs;
  RakNet::PacketPriority priority;
  RakNet::PacketReliability reliability;
  char ordering_channel;
};
bool processing_other_thread;
std::mutex main_thread_mutex;
std::deque<queued_send> main_thread_queue;

void free_packet(RakNet::Packet* pkt)
{
  if (pkt->deleteData) {
    delete[] pkt->data;
  }
  std::free(pkt);
}

RakNet::Packet* create_packet(RakNet::BitStream* bs)
{
  auto* pkt = static_cast<RakNet::Packet*>(std::malloc(sizeof(RakNet::Packet) + bs->GetNumberOfBytesUsed()));
  pkt->data = reinterpret_cast<unsigned char*>(pkt) + sizeof(RakNet::Packet);
  std::memcpy(pkt->data, bs->GetData(), bs->GetNumberOfBytesUsed());
  pkt->length = bs->GetNumberOfBytesUsed();
  pkt->playerId = RakNet::UNASSIGNED_PLAYER_ID;
  pkt->playerIndex = RakNet::UNASSIGNED_PLAYER_INDEX;
  pkt->bitSize = bs->GetNumberOfBitsUsed();
  pkt->deleteData = false;
  return pkt;
}

RakNet::Packet* create_rpc_packet(std::uint8_t id, RakNet::BitStream* bs)
{
  RakNet::BitStream target;
  target.Write<std::uint8_t>(RakNet::ID_RPC);
  target.Write(id);

  if (bs != nullptr) {
    bs->SetReadOffset(0);
    target.WriteCompressed<std::uint32_t>(bs->GetNumberOfBitsUsed());
    target.Write(bs);
  } else {
    target.WriteCompressed<std::uint32_t>(0);
  }

  return create_packet(&target);
}

std::tuple<bool, std::uint8_t, RakNet::BitStream*, int, int, char>
out_packet(RakNet::BitStream* bs, int priority, int reliability, int ordering_channel)
{
  bool send = true;
  std::uint8_t packet_id = bs->GetData()[0];
  if (packet_id == RakNet::ID_TIMESTAMP && bs->GetNumberOfBytesUsed() >= 6) {
    packet_id = bs->GetData()[5];
  }
  std::uint8_t new_packet_id = packet_id;
  RakNet::BitStream* new_bs = nullptr;
  int new_priority = priority;
  int new_reliability = reliability;
  char new_ordering_channel = ordering_channel;

  game::netinfo::handle_outcoming_packet(packet_id, bs);
  lua::script_manager::for_each_event_handler(
      "onSendPacket",
      [packet_id, bs, priority, reliability, ordering_channel,
          &send, &new_packet_id, &new_bs, &new_priority, &new_reliability, &new_ordering_channel](sol::protected_function fn) -> std::optional<sol::error> {
        bs->ResetReadPointer();
        auto pfr = fn(packet_id, bs, priority, reliability, ordering_channel);
        if (!pfr.valid()) {
          sol::error e = pfr;
          return e;
        }

        auto [t_send, t_id, t_bs,
            t_priority, t_reliability, t_ordering_channel]
            = pfr.get<std::tuple<std::optional<bool>,
                std::optional<std::uint8_t>,
                std::optional<RakNet::BitStream*>,
                std::optional<int>,
                std::optional<int>,
                std::optional<int>>>();

        if (t_send.has_value() && !*t_send) {
          send = false;
        }
        if (t_id.has_value()) {
          new_packet_id = *t_id;
        }
        if (t_bs.has_value()) {
          new_bs = static_cast<RakNet::BitStream*>(*t_bs);
        }
        if (t_priority.has_value()) {
          new_priority = *t_priority;
        }
        if (t_reliability.has_value()) {
          new_reliability = *t_reliability;
        }
        if (t_ordering_channel.has_value()) {
          new_ordering_channel = *t_ordering_channel;
        }

        return std::nullopt;
      });

  return { send, new_packet_id, new_bs, new_priority, new_reliability, new_ordering_channel };
}

std::tuple<bool, int, RakNet::BitStream*, int, int, char, bool>
out_rpc(int rpc_id, RakNet::BitStream* bs, int priority, int reliability, int ordering_channel, bool shift_ts)
{
  bool send = true;
  int new_rpc_id = rpc_id;
  RakNet::BitStream* new_bs = nullptr;
  int new_priority = priority;
  int new_reliability = reliability;
  char new_ordering_channel = ordering_channel;
  bool new_shift_ts = shift_ts;

  if (!game::netinfo::handle_outcoming_rpc(rpc_id, bs)) {
    return { false, new_rpc_id, new_bs, new_priority, new_reliability, new_ordering_channel, new_shift_ts };
  }

  lua::script_manager::for_each_event_handler(
      "onSendRpc",
      [rpc_id, bs, priority, reliability, ordering_channel, shift_ts,
          &send, &new_rpc_id, &new_bs, &new_priority, &new_reliability, &new_ordering_channel, &new_shift_ts](sol::protected_function fn) -> std::optional<sol::error> {
        bs->ResetReadPointer();
        auto pfr = fn(rpc_id, bs, priority, reliability, ordering_channel, shift_ts);
        if (!pfr.valid()) {
          sol::error e = pfr;
          return e;
        }

        auto [t_send, t_id, t_bs,
            t_priority, t_reliability, t_ordering_channel, t_shift_ts]
            = pfr.get<std::tuple<std::optional<bool>,
                std::optional<int>,
                std::optional<RakNet::BitStream*>,
                std::optional<int>,
                std::optional<int>,
                std::optional<int>,
                std::optional<bool>>>();

        if (t_send.has_value() && !*t_send) {
          send = false;
        }
        if (t_id.has_value()) {
          new_rpc_id = *t_id;
        }
        if (t_bs.has_value()) {
          new_bs = static_cast<RakNet::BitStream*>(*t_bs);
        }
        if (t_priority.has_value()) {
          new_priority = *t_priority;
        }
        if (t_reliability.has_value()) {
          new_reliability = *t_reliability;
        }
        if (t_ordering_channel.has_value()) {
          new_ordering_channel = *t_ordering_channel;
        }
        if (t_shift_ts.has_value()) {
          new_shift_ts = *t_shift_ts;
        }

        return std::nullopt;
      });

  return { send, new_rpc_id, new_bs, new_priority, new_reliability, new_ordering_channel, new_shift_ts };
}

monethook::hook<bool(RakClientInterface*, RakNet::BitStream*, RakNet::PacketPriority, RakNet::PacketReliability, char)> o_rci_send;
bool h_rci_send(RakClientInterface* self, RakNet::BitStream* bitStream, RakNet::PacketPriority priority, RakNet::PacketReliability reliability, char orderingChannel)
{
  static const thread_local pid_t THREAD_ID = gettid();
  if (!bitStream->GetNumberOfBitsUsed()) {
    return o_rci_send(self, bitStream, priority, reliability, orderingChannel);
  }
  if (lua::script_manager::get_main_thread_id() != THREAD_ID) {
    queued_send send {};
    send.is_rpc = false;
    send.priority = priority;
    send.reliability = reliability;
    send.ordering_channel = orderingChannel;
    bitStream->ResetReadPointer();
    send.bs.Write(bitStream);

    std::lock_guard<std::mutex> lock { main_thread_mutex };
    main_thread_queue.push_back(send);
    return true;
  } else if (!processing_other_thread) {
    rakhook::process(); // Ensure relative order
  }

  std::uint8_t packet_id = bitStream->GetData()[0];
  if (packet_id == RakNet::ID_TIMESTAMP && bitStream->GetNumberOfBytesUsed() >= 6) {
    packet_id = bitStream->GetData()[5];
  }
  auto [send, new_packet_id, new_bs,
      new_priority, new_reliability, new_ordering_channel]
      = out_packet(
          bitStream, priority, reliability, orderingChannel);

  if (!send) {
    return true;
  }

  if (packet_id != new_packet_id && (!new_bs || new_bs == bitStream)) {
    const int old_offset = bitStream->GetWriteOffset();
    if (packet_id == RakNet::ID_TIMESTAMP) {
      bitStream->SetWriteOffset(8 + 32);
    } else {
      bitStream->SetWriteOffset(0);
    }
    bitStream->Write(new_packet_id);
    bitStream->SetWriteOffset(old_offset);
  }

  bool result = o_rci_send(self, new_bs ? new_bs : bitStream,
      static_cast<RakNet::PacketPriority>(new_priority),
      static_cast<RakNet::PacketReliability>(new_reliability),
      new_ordering_channel);
  // if (new_bs && new_bs != bitStream) {
  //   delete new_bs;
  // }
  return result;
}

monethook::hook<bool(RakClientInterface*, int*, RakNet::BitStream*, RakNet::PacketPriority, RakNet::PacketReliability, char, bool, RakNet::NetworkID, RakNet::BitStream*)> o_rci_rpc;
bool h_rci_rpc(RakClientInterface* self, int* uniqueID, RakNet::BitStream* bitStream, RakNet::PacketPriority priority, RakNet::PacketReliability reliability, char orderingChannel, bool shiftTimestamp,
    RakNet::NetworkID networkID, RakNet::BitStream* replyFromTarget)
{
  static const thread_local pid_t THREAD_ID = gettid();
  int id = *uniqueID;
  if (lua::script_manager::get_main_thread_id() != THREAD_ID) {
    queued_send send {};
    send.is_rpc = true;
    send.rpc_id = id;
    send.priority = priority;
    send.reliability = reliability;
    send.ordering_channel = orderingChannel;
    bitStream->ResetReadPointer();
    send.bs.Write(bitStream);

    std::lock_guard<std::mutex> lock { main_thread_mutex };
    main_thread_queue.push_back(send);
    return true;
  } else if (!processing_other_thread) {
    rakhook::process(); // Ensure relative order
  }

  auto [send, new_packet_id, new_bs,
      new_priority, new_reliability, new_ordering_channel, new_shift_ts]
      = out_rpc(id, bitStream, priority, reliability, orderingChannel, shiftTimestamp);

  if (!send) {
    return true;
  }

  bool result = o_rci_rpc(self, &new_packet_id, new_bs ? new_bs : bitStream,
      static_cast<RakNet::PacketPriority>(new_priority),
      static_cast<RakNet::PacketReliability>(new_reliability),
      new_ordering_channel, new_shift_ts, networkID, replyFromTarget);
  // if (new_bs && new_bs != bitStream) {
  //   delete new_bs;
  // }
  return result;
}

std::tuple<bool, std::uint8_t, RakNet::BitStream*> in_rpc(std::uint8_t rpc_id, RakNet::BitStream* bs)
{
  bool send = true;
  std::uint8_t new_rpc_id = rpc_id;
  RakNet::BitStream* new_bs = nullptr;

  lua::script_manager::for_each_event_handler(
      "onReceiveRpc",
      [rpc_id, bs, &send, &new_rpc_id, &new_bs](sol::protected_function fn) -> std::optional<sol::error> {
        bs->ResetReadPointer();
        auto pfr = fn(rpc_id, bs);
        if (!pfr.valid()) {
          sol::error e = pfr;
          return e;
        }

        auto [t_send, t_id, t_bs]
            = pfr.get<std::tuple<std::optional<bool>,
                std::optional<std::uint8_t>,
                std::optional<RakNet::BitStream*>>>();

        if (t_send.has_value() && !*t_send) {
          send = false;
        }
        if (t_id.has_value()) {
          new_rpc_id = *t_id;
        }
        if (t_bs.has_value()) {
          new_bs = static_cast<RakNet::BitStream*>(*t_bs);
        }

        return std::nullopt;
      });

  if (send) {
    if (!game::netinfo::handle_incoming_rpc(new_rpc_id, new_bs ? new_bs : bs)) {
      send = false;
    }
  }

  return { send, new_rpc_id, new_bs };
}

std::tuple<bool, int, RakNet::BitStream*> in_packet(RakNet::BitStream* bs)
{
  bool send = true;
  std::uint8_t packet_id = bs->GetData()[0];
  if (packet_id == RakNet::ID_TIMESTAMP && bs->GetNumberOfBytesUsed() >= 6) {
    packet_id = bs->GetData()[5];
  }
  std::uint8_t new_packet_id = packet_id;
  RakNet::BitStream* new_bs = nullptr;

  lua::script_manager::for_each_event_handler(
      "onReceivePacket",
      [packet_id, bs, &send, &new_packet_id, &new_bs](sol::protected_function fn) -> std::optional<sol::error> {
        bs->ResetReadPointer();
        auto pfr = fn(packet_id, bs);
        if (!pfr.valid()) {
          sol::error e = pfr;
          return e;
        }

        auto [t_send, t_id, t_bs]
            = pfr.get<std::tuple<std::optional<bool>,
                std::optional<std::uint8_t>,
                std::optional<RakNet::BitStream*>>>();

        if (t_send.has_value() && !*t_send) {
          send = false;
        }
        if (t_id.has_value()) {
          new_packet_id = *t_id;
        }
        if (t_bs.has_value()) {
          new_bs = *t_bs;
        }

        return std::nullopt;
      });

  if (send) {
    game::netinfo::handle_incoming_packet(new_packet_id, new_bs ? new_bs : bs);
  }

  return { send, new_packet_id, new_bs };
}

monethook::hook<RakNet::Packet*(void*)> o_rakpeer_receiveignorerpc;
RakNet::Packet* h_rakpeer_receiveignorerpc(void* self)
{
  // Never happens as of 04.07.2023. Uncomment on possible crashes
  // static const thread_local pid_t THREAD_ID = gettid();
  // if (lua::script_manager::get_main_thread_id() != THREAD_ID) {
  //   return o_rakpeer_receiveignorerpc(self);
  // }

  game::netinfo::reset_add_remove();

  if (!emulation_queue.empty()) {
    RakNet::Packet* pkt = emulation_queue.front();
    emulation_queue.pop_front();

    if (pkt->length == 0) {
      return pkt;
    }

    if (pkt->data[0] == RakNet::ID_RPC || (pkt->data[0] == RakNet::ID_TIMESTAMP && pkt->length >= 6 && pkt->data[5] == RakNet::ID_RPC)) {
      const std::uint32_t skip = pkt->data[0] == RakNet::ID_TIMESTAMP ? 6 : 1;
      std::int32_t number_of_bits;
      RakNet::BitStream temp { pkt->data + skip, pkt->length - skip, false };
      std::uint8_t id;
      if (!temp.Read(id) || !temp.ReadCompressed(number_of_bits) || number_of_bits < 0) {
        return pkt;
      }

      RakNet::BitStream bs {};
      bs.Write(&temp, number_of_bits);
      if (game::netinfo::handle_incoming_rpc(id, &bs)) {
        return pkt;
      } else {
        free_packet(pkt);
      }
    } else {
      RakNet::BitStream bs { pkt->data, pkt->length, false };
      std::uint8_t packet_id = bs.GetData()[0];
      if (packet_id == RakNet::ID_TIMESTAMP && bs.GetNumberOfBytesUsed() >= 6) {
        packet_id = bs.GetData()[5];
      }
      game::netinfo::handle_incoming_packet(packet_id, &bs);
      return pkt;
    }
  }
  if (emulation_only) {
    return nullptr;
  }

  RakNet::Packet* pkt = o_rakpeer_receiveignorerpc(self);
  if (!pkt || pkt->length == 0) {
    return pkt;
  }

  if (pkt->data[0] == RakNet::ID_RPC || (pkt->data[0] == RakNet::ID_TIMESTAMP && pkt->length >= 6 && pkt->data[5] == RakNet::ID_RPC)) {
    const std::uint32_t skip = pkt->data[0] == RakNet::ID_TIMESTAMP ? 6 : 1;
    std::int32_t number_of_bits;
    RakNet::BitStream temp { pkt->data + skip, pkt->length - skip, false };
    std::uint8_t id;
    if (!temp.Read(id) || !temp.ReadCompressed(number_of_bits) || number_of_bits < 0) {
      return pkt;
    }

    RakNet::BitStream bs {};
    bs.Write(&temp, number_of_bits);

    auto [send, new_id, new_bs] = in_rpc(id, &bs);
    if (!send) {
      free_packet(pkt);
      return nullptr;
    }

    RakNet::Packet* new_pkt = create_rpc_packet(new_id, new_bs ? new_bs : &bs);
    free_packet(pkt);
    // if (new_bs && new_bs != &bs) {
    //   delete new_bs;
    // }

    return new_pkt;
  } else {
    RakNet::BitStream bs { pkt->data, pkt->length, false };
    std::uint8_t packet_id = bs.GetData()[0];
    if (packet_id == RakNet::ID_TIMESTAMP && bs.GetNumberOfBytesUsed() >= 6) {
      packet_id = bs.GetData()[5];
    }
    auto [send, new_id, new_bs] = in_packet(&bs);
    if (!send) {
      free_packet(pkt);
      return nullptr;
    }

    if (new_id != packet_id || new_bs || bs.GetData() != pkt->data) {
      if (!new_bs) {
        const int old_offset = bs.GetWriteOffset();
        if (packet_id == RakNet::ID_TIMESTAMP) {
          bs.SetWriteOffset(8 + 32);
        } else {
          bs.SetWriteOffset(0);
        }
        bs.Write(new_id);
        bs.SetWriteOffset(old_offset);
      }

      RakNet::Packet* new_pkt = create_packet(new_bs ? new_bs : &bs);
      free_packet(pkt);
      // if (new_bs && new_bs != &bs) {
      //   delete new_bs;
      // }

      return new_pkt;
    }
  }

  return pkt;
}
}

void rakhook::init(RakClientInterface* client, std::uintptr_t recv_ignore_rpc)
{
  rci = client;

  // A hook that could not be installed leaves its target untouched, so the
  // loader would run with a half hooked network and nothing would say so.
  // The interface is only reported as ready when all three hooks are in.
  auto install = [](auto& target, const char* name) {
    const auto result = target.apply();
    if (result != monethook::detail::hook_result::success) {
      logger::log<logger::LV_ERROR>(nullptr, "RakNet hook {} was not installed: {}",
          name, static_cast<int>(result));
      return false;
    }
    return true;
  };

  o_rci_send = { reinterpret_cast<std::uintptr_t>(rci->vtbl->Send), h_rci_send };
  o_rci_rpc = { reinterpret_cast<std::uintptr_t>(rci->vtbl->RPC), h_rci_rpc };
  o_rakpeer_receiveignorerpc = { recv_ignore_rpc, h_rakpeer_receiveignorerpc };

  bool ready = install(o_rci_send, "Send");
  ready = install(o_rci_rpc, "RPC") && ready;
  ready = install(o_rakpeer_receiveignorerpc, "ReceiveIgnoreRPC") && ready;

  RakNet::StringCompressor::AddReference();
  inited = ready;

  if (ready) {
    logger::log<logger::LV_SYSTEM>(nullptr, "{}", "the RakNet hooks are in place");
  } else {
    logger::log<logger::LV_ERROR>(nullptr, "{}", "the RakNet interface of the client was not hooked");
  }
}

bool rakhook::available()
{
  return inited;
}

void rakhook::set_client(RakClientInterface* client)
{
  rci = client;
}

void rakhook::process()
{
  if (processing_other_thread) {
    return;
  }

  std::lock_guard<std::mutex> lock { main_thread_mutex };

  processing_other_thread = true;
  for (auto& i : main_thread_queue) {
    if (i.is_rpc) {
      h_rci_rpc(rci, &i.rpc_id, &i.bs, i.priority, i.reliability, i.ordering_channel,
          false, RakNet::UNASSIGNED_NETWORK_ID, nullptr);
    } else {
      h_rci_send(rci, &i.bs, i.priority, i.reliability, i.ordering_channel);
    }
  }
  main_thread_queue.clear();
  processing_other_thread = false;
}

void rakhook::emu::clear()
{
  emulation_queue.clear();
}

void rakhook::emu::only(bool toggle)
{
  emulation_only = toggle;
}

void rakhook::emu::packet(RakNet::BitStream* bs)
{
  if (!bs)
    return;

  RakNet::Packet* pkt = create_packet(bs);
  emulation_queue.push_back(pkt);
}

void rakhook::emu::rpc(uint8_t id, RakNet::BitStream* bs)
{
  RakNet::Packet* pkt = create_rpc_packet(id, bs);
  emulation_queue.push_back(pkt);
}

void rakhook::send::packet(RakNet::BitStream* bs, RakNet::PacketPriority priority, RakNet::PacketReliability reliability, char ordering_channel)
{
  rakhook::process(); // Ensure relative order
  o_rci_send(rci, bs, priority, reliability, ordering_channel);
}

void rakhook::send::packet_invoke_handlers(RakNet::BitStream* bs, RakNet::PacketPriority priority, RakNet::PacketReliability reliability, char ordering_channel)
{
  h_rci_send(rci, bs, priority, reliability, ordering_channel);
}

void rakhook::send::rpc(int id, RakNet::BitStream* bs, RakNet::PacketPriority priority, RakNet::PacketReliability reliability, char ordering_channel)
{
  rakhook::process(); // Ensure relative order
  o_rci_rpc(rci, &id, bs, priority, reliability, ordering_channel, false, RakNet::UNASSIGNED_NETWORK_ID, nullptr);
}

void rakhook::send::rpc_invoke_handlers(int id, RakNet::BitStream* bs, RakNet::PacketPriority priority, RakNet::PacketReliability reliability, char ordering_channel)
{
  h_rci_rpc(rci, &id, bs, priority, reliability, ordering_channel, false, RakNet::UNASSIGNED_NETWORK_ID, nullptr);
}