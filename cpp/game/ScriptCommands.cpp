/*
  Plugin-SDK (Grand Theft Auto) SHARED source file
  Authors: GTA Community. See more here
  https://github.com/DK22Pac/plugin-sdk
  Do not delete this comment block. Respect others' work!
*/

#include "ScriptCommands.h"
#include <unistd.h>
#include <cinttypes>
#include <csetjmp>
#include <cstdio>
#include <csignal>
#include <stdexcept>
// #include "CPools.h"

namespace {
struct sigaction g_sigaction;
struct sigaction g_old_sigaction_segv;
struct sigaction g_old_sigaction_bus;
struct sigaction g_old_sigaction_ill;
struct sigaction g_old_sigaction_fpe;
struct sigaction g_old_sigaction_trap;

sigjmp_buf g_safe_process_jmpbuf;
volatile bool g_processing_command;
volatile std::uintptr_t g_error_address;
volatile int g_error_signal;

void call_sigaction(struct sigaction* sa, int sig, siginfo_t* info, void* reserved)
{
  if (sa->sa_handler == SIG_DFL) {
    signal(sig, SIG_DFL);
    raise(sig);
    sigaction(sig, &g_sigaction, sa);
    return;
  }
  if (sa->sa_handler == SIG_IGN) {
    return;
  }

  if (sa->sa_flags & SA_SIGINFO) {
    sa->sa_sigaction(sig, info, reserved);
  } else {
    sa->sa_handler(sig);
  }
}

void scripting_signal_handler(int signal, siginfo_t* info, void* reserved)
{
  if (g_processing_command) {
    g_error_signal = signal;
    g_error_address = reinterpret_cast<std::uintptr_t>(info->si_addr);
    siglongjmp(g_safe_process_jmpbuf, 1);
  } else {
    switch (signal) {
    case SIGSEGV: {
			call_sigaction(&g_old_sigaction_segv, signal, info, reserved);
      break;
    }
    case SIGBUS: {
      call_sigaction(&g_old_sigaction_bus, signal, info, reserved);
      break;
    }
    case SIGILL: {
      call_sigaction(&g_old_sigaction_ill, signal, info, reserved);
      break;
    }
    case SIGFPE: {
      call_sigaction(&g_old_sigaction_fpe, signal, info, reserved);
      break;
    }
    case SIGTRAP: {
      call_sigaction(&g_old_sigaction_trap, signal, info, reserved);
      break;
		}
    }
  }
}
}

using namespace game;

scripting::ScriptCode::VarToSet::VarToSet(unsigned int _varIndex, void* _pVar, ScriptResultVarType _varType)
{
  varIndex = _varIndex;
  pVar = _pVar;
  varType = _varType;
}

scripting::ScriptCode::ScriptCode(short commandId)
{
  data = dataBuffer;
  capacity = sizeof(dataBuffer);
  size = 0;
  allocated = false;

  varsToSetIndex = 0;
  varIndexCounter = 0;

  if (commandId != -1) {
    AddBytes(reinterpret_cast<unsigned char*>(&commandId), 2);
  }
}

scripting::ScriptCode::~ScriptCode()
{
  if (allocated) {
    delete[] data;
  }
}

void scripting::ScriptCode::AddParameterDescription(unsigned char paramType)
{
  AddBytes(&paramType, 1);
}

void scripting::ScriptCode::AddBytes(unsigned char* bytes, unsigned int count)
{
  unsigned int newSize = size + count;
  if (newSize > capacity) {
    auto* new_data = new unsigned char[newSize];
    memcpy(new_data, data, size);
    if (allocated) {
      delete[] data;
    }
    data = new_data;
    allocated = true;
  }
  memcpy(&data[size], bytes, count);
  size = newSize;
}

unsigned char* scripting::ScriptCode::GetData() { return data; };

void scripting::ScriptCode::SaveResultVariables(CRunningScript* script)
{
  for (int i = 0; i < varsToSetIndex; ++i) {
    auto& varToSet = varsToSet[i];
    if (varToSet.varType == SCRIPT_RESULT_VAR_NUMBER) {
      *reinterpret_cast<unsigned int*>(varToSet.pVar) = script->m_aLocalVars[varToSet.varIndex].uParam;
    } else if (varToSet.varType == SCRIPT_RESULT_VAR_STRING) {
      char* pStr = reinterpret_cast<char*>(varToSet.pVar);
      strncpy(pStr, reinterpret_cast<char*>(&script->m_aLocalVars[varToSet.varIndex].iParam), 15);
      pStr[15] = '\0';
    }
    // } else if (varToSet.varType == SCRIPT_RESULT_VAR_PED) {
    // 	CPed* result = nullptr;
    // 	if (script->m_aLocalVars[varToSet.varIndex].iParam != -1)
    // 		result = CPools::GetPed(script->m_aLocalVars[varToSet.varIndex].iParam);
    // 	*reinterpret_cast<CPed**>(varToSet.pVar) = result;
    // } else if (varToSet.varType == SCRIPT_RESULT_VAR_VEHICLE) {
    // 	CVehicle* result = nullptr;
    // 	if (script->m_aLocalVars[varToSet.varIndex].iParam != -1)
    // 		result = CPools::GetVehicle(script->m_aLocalVars[varToSet.varIndex].iParam);
    // 	*reinterpret_cast<CVehicle**>(varToSet.pVar) = result;
    // } else if (varToSet.varType == SCRIPT_RESULT_VAR_OBJECT) {
    // 	CObject* result = nullptr;
    // 	if (script->m_aLocalVars[varToSet.varIndex].iParam != -1)
    // 		result = CPools::GetObject(script->m_aLocalVars[varToSet.varIndex].iParam);
    // 	*reinterpret_cast<CObject**>(varToSet.pVar) = result;
    // }
  }
}

void scripting::ScriptCode::operator<<(char n)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_INT_8BITS);
  AddBytes(reinterpret_cast<unsigned char*>(&n), 1);
}
void scripting::ScriptCode::operator<<(unsigned char n)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_INT_8BITS);
  AddBytes(reinterpret_cast<unsigned char*>(&n), 1);
}
void scripting::ScriptCode::operator<<(short n)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_INT_16BITS);
  AddBytes(reinterpret_cast<unsigned char*>(&n), 2);
}
void scripting::ScriptCode::operator<<(unsigned short n)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_INT_16BITS);
  AddBytes(reinterpret_cast<unsigned char*>(&n), 2);
}
void scripting::ScriptCode::operator<<(int n)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_INT_32BITS);
  AddBytes(reinterpret_cast<unsigned char*>(&n), 4);
}
void scripting::ScriptCode::operator<<(unsigned int n)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_INT_32BITS);
  AddBytes(reinterpret_cast<unsigned char*>(&n), 4);
}
void scripting::ScriptCode::operator<<(float n)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_FLOAT);
  AddBytes(reinterpret_cast<unsigned char*>(&n), 4);
}
void scripting::ScriptCode::operator<<(double n)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_FLOAT);
  auto f = static_cast<float>(n);
  AddBytes(reinterpret_cast<unsigned char*>(&f), 4);
}
void scripting::ScriptCode::operator<<(ScriptCommandEndParameter) { AddParameterDescription(SCRIPTPARAM_END_OF_ARGUMENTS); }

void scripting::ScriptCode::operator<<(char* str)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_PASCAL_STRING);
  unsigned int length = strlen(str);
  AddParameterDescription(length);
  AddBytes(reinterpret_cast<unsigned char*>(str), length);
}

void scripting::ScriptCode::operator<<(const char* str)
{
  AddParameterDescription(SCRIPTPARAM_STATIC_PASCAL_STRING);
  unsigned int length = strlen(str);
  AddParameterDescription(length);
  AddBytes(reinterpret_cast<unsigned char*>(const_cast<char*>(str)), length);
}

void scripting::ScriptCode::operator<<(char (*p)[16])
{
  AddParameterDescription(SCRIPTPARAM_LOCAL_LONG_STRING_VARIABLE);
  varsToSet[varsToSetIndex++] = VarToSet { varIndexCounter, reinterpret_cast<void*>(*p), SCRIPT_RESULT_VAR_STRING };
  AddBytes(reinterpret_cast<unsigned char*>(&varIndexCounter), 2);
  varIndexCounter += 4;
}

void scripting::ScriptCode::operator<<(float* p)
{
  AddParameterDescription(SCRIPTPARAM_LOCAL_NUMBER_VARIABLE);
  varsToSet[varsToSetIndex++] = VarToSet { varIndexCounter, reinterpret_cast<void*>(p), SCRIPT_RESULT_VAR_NUMBER };
  AddBytes(reinterpret_cast<unsigned char*>(&varIndexCounter), 2);
  ++varIndexCounter;
}

void scripting::ScriptCode::operator<<(int* p)
{
  AddParameterDescription(SCRIPTPARAM_LOCAL_NUMBER_VARIABLE);
  varsToSet[varsToSetIndex++] = VarToSet { varIndexCounter, reinterpret_cast<void*>(p), SCRIPT_RESULT_VAR_NUMBER };
  AddBytes(reinterpret_cast<unsigned char*>(&varIndexCounter), 2);
  ++varIndexCounter;
}

void scripting::ScriptCode::operator<<(unsigned int* p)
{
  AddParameterDescription(SCRIPTPARAM_LOCAL_NUMBER_VARIABLE);
  varsToSet[varsToSetIndex++] = VarToSet { varIndexCounter, reinterpret_cast<void*>(p), SCRIPT_RESULT_VAR_NUMBER };
  AddBytes(reinterpret_cast<unsigned char*>(&varIndexCounter), 2);
  ++varIndexCounter;
}

// void scripting::ScriptCode::operator<<(CPed* n)
// {
// 	if (!n)
// 		operator<<(-1);
// 	else
// 		operator<<(CPools::GetPedRef(n));
// }

// void scripting::ScriptCode::operator<<(CVehicle* n)
// {
// 	if (!n)
// 		operator<<(-1);
// 	else
// 		operator<<(CPools::GetVehicleRef(n));
// }

// void scripting::ScriptCode::operator<<(CObject* n)
// {
// 	if (!n)
// 		operator<<(-1);
// 	else
// 		operator<<(CPools::GetObjectRef(n));
// }

// void scripting::ScriptCode::operator<<(CPed** p)
// {
// 	AddParameterDescription(SCRIPTPARAM_LOCAL_NUMBER_VARIABLE);
// 	varsToSet.emplace_back(varIndexCounter, reinterpret_cast<void*>(p), SCRIPT_RESULT_VAR_PED);
// 	AddBytes(reinterpret_cast<unsigned char*>(&varIndexCounter), 2);
// 	++varIndexCounter;
// }

// void scripting::ScriptCode::operator<<(CVehicle** p)
// {
// 	AddParameterDescription(SCRIPTPARAM_LOCAL_NUMBER_VARIABLE);
// 	varsToSet.emplace_back(varIndexCounter, reinterpret_cast<void*>(p), SCRIPT_RESULT_VAR_VEHICLE);
// 	AddBytes(reinterpret_cast<unsigned char*>(&varIndexCounter), 2);
// 	++varIndexCounter;
// }

// void scripting::ScriptCode::operator<<(CObject** p)
// {
// 	AddParameterDescription(SCRIPTPARAM_LOCAL_NUMBER_VARIABLE);
// 	varsToSet.emplace_back(varIndexCounter, reinterpret_cast<void*>(p), SCRIPT_RESULT_VAR_OBJECT);
// 	AddBytes(reinterpret_cast<unsigned char*>(&varIndexCounter), 2);
// 	++varIndexCounter;
// }

void game::scripting::InitSafeProcessing()
{
  static bool installed = false;
  if (installed) {
    return; // Installing it twice would save our own handler as the "old" one
  }
  installed = true;

  sigemptyset(&g_sigaction.sa_mask);
  g_sigaction.sa_sigaction = scripting_signal_handler;
  g_sigaction.sa_flags = SA_SIGINFO;

  sigaction(SIGSEGV, &g_sigaction, &g_old_sigaction_segv);
  sigaction(SIGBUS, &g_sigaction, &g_old_sigaction_bus);
  sigaction(SIGILL, &g_sigaction, &g_old_sigaction_ill);
  sigaction(SIGFPE, &g_sigaction, &g_old_sigaction_fpe);
  sigaction(SIGTRAP, &g_sigaction, &g_old_sigaction_trap);
}

void game::scripting::SafeProcessOneCommand(CRunningScript& script, unsigned int commandId)
{
  switch (sigsetjmp(g_safe_process_jmpbuf, 1)) {
  case 0: {
    g_processing_command = true;
    script.ProcessOneCommand();
    g_processing_command = false;
    return;
  }

  default: {
    g_processing_command = false;

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        "got signal #%d trying to access address 0x%" PRIxPTR " while processing opcode %04x",
        g_error_signal, g_error_address, commandId);
    throw std::runtime_error { buffer };
    return;
  }
  }
}