#pragma once
#include "CCopPed.h"
#include "CCutsceneObject.h"
#include "CHeli.h"
#include "CObject.h"
#include "CPed.h"
#include "CPlane.h"
#include "CPool.h"
#include "CVehicle.h"

#include "gta_struct.inl"
namespace CPools {
namespace detail {
  template <bool, typename T1, typename T2>
  struct is_cond {
    using type = T1;
  };

  template <typename T1, typename T2>
  struct is_cond<false, T1, T2> {
    using type = T2;
  };

  template <typename T1, typename T2>
  struct largest {
    using type = typename is_cond<(sizeof(T1) > sizeof(T2)), T1, T2>::type;
  };

  /* The pools of the game are created while it loads, and the scripts are
     started before that point: a script that asks for a ped at that moment
     would make the game read the size of a null pool. An empty pool is handed
     out until the game has a pool of its own, so the lookup answers "no such
     object" instead of taking the process down. */
  template <typename T, typename B>
  CPool<T, B>& empty_pool()
  {
    static CPool<T, B> pool {};
    return pool;
  }
}

inline CPool<CPed, CCopPed>* GetPedPool()
{
  auto* pool = *reinterpret_cast<CPool<CPed, CCopPed>**>(lib_manager::ped_pool);
  return pool ? pool : &detail::empty_pool<CPed, CCopPed>();
}

inline CPool<CVehicle, detail::largest<CPlane, CHeli>::type>* GetVehiclePool()
{
  auto* pool = *reinterpret_cast<CPool<CVehicle, detail::largest<CPlane, CHeli>::type>**>(lib_manager::vehicle_pool);
  return pool ? pool : &detail::empty_pool<CVehicle, detail::largest<CPlane, CHeli>::type>();
}

inline CPool<CObject, CCutsceneObject>* GetObjectPool()
{
  auto* pool = *reinterpret_cast<CPool<CObject, CCutsceneObject>**>(lib_manager::object_pool);
  return pool ? pool : &detail::empty_pool<CObject, CCutsceneObject>();
}
}
#include "gta_struct.inl"
