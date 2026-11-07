/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Frozen pre323 production predicate; only its name is changed for comparison. */
bool baseline_logic_due(uint64_t now,uint64_t owner_until,bool owner_valid){
 uint32_t revision=(uint32_t)atomic_get(&context_revision),hint=(uint32_t)atomic_get(&source_hint);
 unsigned key=ui_take();
 bool changed=model_context!=revision||model_hint!=hint;
 uint64_t until=model.ble_valid_until_us;
 bool held=model.profile_rc&&model.ble_local_holdover;
 ui_drop(key);
 bool near=now<=(UINT64_MAX-1000)/1000&&until&&until<=now*1000+1000&&
  ((owner_valid&&owner_until>until)||held);
 if(now<logic_due_ms&&!changed&&!near)return false;
 logic_due_ms=now+20;return true;
}
