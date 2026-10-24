/* Bridge the direct Ivory interpreter to the VLM host-services ABI. */
#include "std.h"
#include "ivoryrep.h"
#include "../emulator/memory.h"
#include "utilities.h"
#include "traps.h"
#undef ldb
#undef dpb
#include "emulator.h"
#include "ivory.h"
#include "dispatch.h"


static CProcessorState state;
CProcessorState *c_processor = &state;
const CObject ObjectT = COBJECT(TypeSymbol, AddressT);
const CObject ObjectNIL = COBJECT(TypeNIL, AddressNIL);
const CObject ObjectCdrMask = COBJECT(TagCdrMask, 0);

#define SCALAR_FIELDS(X) \
 X(control,control) X(StackCacheBase,stackcachebasevma) \
 X(ArrayEventCount,areventcount) X(ListCacheLength,lclength) \
 X(StructureCacheLength,sclength) \
 X(BindingStackLimit,bindingstacklimit) X(PreemptRegister,interruptreg) \
 X(AluAndRotateControl,aluandrotatecontrol) X(ByteSize,bytesize) \
 X(ByteRotate,byterotate) X(RotateLatch,rotatelatch) X(ALUOverflow,aluoverflow) \
 X(ALUBorrow,aluborrow) X(ALULessThan,alulessthan) \
 X(EphemeralOldspaceRegister,ephemeraloldspace) X(ZoneOldspaceRegister,zoneoldspace) \
 X(ControlStackLimit,cslimit) X(ControlStackExtraLimit,csextralimit) \
 X(DynamicBindingCacheBase,dbcbase) X(DynamicBindingCacheMask,dbcmask) \
 X(ChoicePointer,choiceptr) X(StructureStackChoicePointer,sstkchoiceptr) \
 X(FEPModeTrapVectorAddress,fepmodetrapvecaddress)
#define OBJECT_FIELDS(X) \
 X(continuation,continuation) X(ListCacheArea,lcarea) X(ListCacheAddress,lcaddress) \
 X(StructureCacheArea,scarea) X(StructureCacheAddress,scaddress) X(CatchBlockPointer,catchblock) \
 X(bar[0].address,bar0) X(bar[1].address,bar1) X(bar[2].address,bar2) X(bar[3].address,bar3)

void CImportState(void)
{
#define IMPORT(c,h) state.c = processor->h;
  SCALAR_FIELDS(IMPORT)
#undef IMPORT
#define IMPORT(c,h) state.c.whole = processor->h;
  OBJECT_FIELDS(IMPORT)
#undef IMPORT
  state.BindingStackPointer = (uint32_t)processor->bindingstackpointer;
  state.instruction_count = -processor->instruction_count;
  state.StackCacheOverflowLimit = processor->scovlimit;
  state.sp = (CObject *)processor->sp;
  state.fp = (CObject *)processor->fp;
  state.lp = (CObject *)processor->lp;
  state.restartsp = (CObject *)processor->restartsp;
  state.pc.DATA.u = (uint64_t)processor->epc >> 1;
  state.pc.TAG = TypeEvenPC + (processor->epc & 1);
  state.StackCache = (CObject *)processor->stackcachedata;
  state.StackCacheWords = processor->stackcachesize;
  state.StackCacheLimit = state.StackCache + state.StackCacheWords - 256;
  state.AluOp = ReadALUFunctionClass(state.AluAndRotateControl);
  state.DeepBoundP = 0; /* Genera uses shallow binding, as does the VLM. */
}

void CExportState(void)
{
#define EXPORT(c,h) processor->h = state.c;
  SCALAR_FIELDS(EXPORT)
#undef EXPORT
#define EXPORT(c,h) processor->h = state.c.whole;
  OBJECT_FIELDS(EXPORT)
#undef EXPORT
  processor->sp = (intptr_t)state.sp;
  processor->fp = (intptr_t)state.fp;
  processor->lp = (intptr_t)state.lp;
  processor->restartsp = (uintptr_t)state.restartsp;
  processor->epc = ((uint64_t)state.pc.DATA.u << 1) | (state.pc.TAG & 1);
  processor->bindingstackpointer = MakeLispObj(Type_Locative,state.BindingStackPointer);
  processor->instruction_count = -(int64_t)state.instruction_count;
  processor->scovlimit = state.StackCacheOverflowLimit;
  processor->stackcachetopvma = (uint64_t)state.StackCacheBase + state.StackCacheWords;
}

void CFlushInstructionCache(void)
{
  if (!state.InstructionCache) {
    state.InstructionCache = calloc(InstructionCacheSize, sizeof(InstructionCacheLine));
    if (!state.InstructionCache) vpunt(NULL, "Unable to allocate C instruction cache");
  }
  for (unsigned i = 0; i < InstructionCacheSize; ++i) {
    state.InstructionCache[i].pc.TAG = TypeEvenPC + (i & 1);
    state.InstructionCache[i].pc.DATA.u = UINT32_MAX;
    state.InstructionCache[i].code = DispatchInstructionCacheLookup;
  }
}

int CPollInterrupt(void)
{
  if (__atomic_exchange_n(&processor->please_stop, 0, __ATOMIC_RELAXED)) return -1;
  int requested = __atomic_exchange_n(&processor->stop_interpreter, 0, __ATOMIC_RELAXED);
  int trap = __atomic_exchange_n(&processor->please_trap, 0, __ATOMIC_RELAXED);
  unsigned mode = ReadControlTrapMode(state.control);
  if (trap == TrapReason_HighPrioritySequenceBreak)
    return mode < 2 ? HighPrioritySequenceBreakTrapVector : 0;
  if (trap == TrapReason_LowPrioritySequenceBreak)
    return mode == 0 ? LowPrioritySequenceBreakTrapVector : 0;
  if (requested && mode == 0 && (state.PreemptRegister & 1))
    return PreemptRequestTrapVector;
  return 0;
}

void CRequestPreemptCheck(void)
{
  if (state.PreemptRegister & 1)
    __atomic_store_n(&processor->stop_interpreter, 1, __ATOMIC_RELAXED);
}

int iInterpret(PROCESSORSTATEP unused)
{
  CImportState();
  if (!state.InstructionCache) CFlushInstructionCache();
  state.running = 1;
  int reason = CInstructionSequencer();
  CExportState();
  processor->runningp = 0;
  return reason;
}

CObject CCoprocessorRead(unsigned reg)
{
  CExportState();
  CObject result = { .whole = CoprocessorRead(reg) };
  CImportState();
  return result;
}

int CCoprocessorWrite(unsigned reg, CObject value)
{
  CExportState();
  int result;
  if (reg == CoprocessorRegister_FlushIDCaches ||
      reg == CoprocessorRegister_FlushCachesForVMA) {
    CFlushInstructionCache();
    result = TRUE;
  } else if (reg == 650) {
    /* Array accesses do not cache array headers in this interpreter. */
    result = TRUE;
  } else {
    result = CoprocessorWrite(reg, value.whole);
  }
  CImportState();
  return result;
}

static int CheckMemory(Integer vma, int write)
{
  VMAttribute attr = VMAttributeTable[vma >> MemoryPage_AddressShift];
  if (!VMExists(attr)) TakeMemoryTrap(PageNotResidentTrapVector, vma);
  if (VMAccessFault(attr)) TakeMemoryTrap(PageFaultRequestTrapVector, vma);
  if (write && VMWriteFault(attr)) TakeMemoryTrap(PageWriteFaultTrapVector, vma);
  if (VMTransportFault(attr) && !VMTransportDisable(attr) &&
      ReadControlTrapMode(state.control) != TrapModeFEP)
    TakeMemoryTrap(TransportTrapVector, vma);
  /* Transport is disabled in FEP mode; uncached host reads handle protections. */
  if (write && (!VMModified(attr) || !VMEphemeral(attr)))
    RecordVirtualMemoryWrite(vma);
  return VMTransportFault(attr) && !VMTransportDisable(attr);
}

/* Host coprocessor services also access protected guest memory directly.
   Their SIGSEGV handler resolves write barriers and sends real guest faults
   here instead of jumping into the generated Alpha interpreter. */
void CMemoryFault(Integer vma)
{
  VMAttribute attr = VMAttributeTable[vma >> MemoryPage_AddressShift];
  int vector = !VMExists(attr) ? PageNotResidentTrapVector :
               VMAccessFault(attr) ? PageFaultRequestTrapVector :
               VMWriteFault(attr) ? PageWriteFaultTrapVector :
               VMTransportFault(attr) && !VMTransportDisable(attr) ?
                 TransportTrapVector : MemoryBusErrorTrapVector;
  TakeMemoryTrap(vector, vma);
}

int CRawRead(Integer vma, CObject *value)
{
  if (CheckMemory(vma, 0)) {
    /* FEP mode can read a page protected for transport. */
    value->whole = VirtualMemoryReadUncached(vma);
  } else {
    value->TAG = *MapVirtualAddressTag(vma);
    value->DATA.u = *MapVirtualAddressData(vma);
  }
  return 0;
}
int CRawWrite(Integer vma, CObject *value)
{
  if (CheckMemory(vma, 1)) {
    VirtualMemoryWriteUncached(vma, value->whole);
  } else {
    *MapVirtualAddressData(vma) = value->DATA.u;
    *MapVirtualAddressTag(vma) = value->TAG;
  }
  return 0;
}
int CRawReadBlock(Integer vma, CObject *value, int count)
{
  for (int i = 0; i < count; ++i) CRawRead(vma + i, value + i);
  return 0;
}
int CRawWriteBlock(Integer vma, CObject *value, int count)
{
  for (int i = 0; i < count; ++i) CRawWrite(vma + i, value + i);
  return 0;
}
