/* Instruction-level checks shared by both Ivory interpreter backends. */
#include "std.h"
#include "ivoryrep.h"
#include "memory.h"
#include "aihead.h"
#include <limits.h>
#include <math.h>

Boolean Trace = FALSE;
Boolean EnableIDS = FALSE;
Boolean TestFunction = FALSE;
static unsigned tag(LispObj x) { return (uint64_t)x >> 32; }
static uint32_t word(LispObj x) { return x; }
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const Integer code = 0xf8004000, data = 0xf8006000;
static unsigned halfword(unsigned opcode, unsigned mode, unsigned operand)
{ return (opcode << 10) | (mode << 8) | (operand & 255); }
static unsigned immediate(unsigned opcode, unsigned operand)
{ return (opcode << 10) | (operand & 1023); }
static void pair(Integer address, unsigned even, unsigned odd)
{
  VirtualMemoryWriteUncached(address, MakeLispObj(060 | (odd >> 14), even | (odd << 18)));
}
static void reset(void)
{
  processor->stackcachebasevma = 0xf8000100;
  processor->stackcachetopvma = processor->stackcachebasevma + processor->stackcachesize;
  processor->scovlimit = 256;
  processor->fp = (intptr_t)stackcache;
  processor->sp = (intptr_t)(stackcache + 1);
  processor->lp = (intptr_t)(stackcache + 2);
  processor->restartsp = processor->sp;
  processor->control = 2 | (3u << 30);
  processor->continuation = MakeLispObj(Type_NIL, 0);
  processor->epc = (uint64_t)code << 1;
  processor->please_stop = processor->please_trap = 0;
  processor->interruptreg = 0;
  processor->cslimit = processor->stackcachebasevma + 0x1000;
  processor->csextralimit = processor->stackcachebasevma + 0x1800;
  processor->runningp = 1;
  memset(stackcache, 0, processor->stackcachesize * sizeof(*stackcache));
  flushicache();
}
static void push(unsigned tag, uint32_t value)
{ processor->sp += sizeof(LispObj); *(LispObj *)processor->sp = MakeLispObj(tag, value); }
static void fix(int32_t value) { push(Type_Fixnum, value); }
static LispObj tos(int offset) { return ((LispObj *)processor->sp)[offset]; }
static int32_t num(int offset)
{ CHECK((tag(tos(offset)) & 63) == Type_Fixnum); return word(tos(offset)); }
static void run(unsigned instruction)
{
  pair(code, instruction, halfword(Opcode_Halt, 0, 0));
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0)) == HaltReason_Halted);
}
static void binary(unsigned opcode, int32_t x, int32_t y, int32_t result)
{
  reset(); fix(x); fix(y); run(halfword(opcode, 2, 0));
  if(num(0)!=result) fprintf(stderr,"opcode %o: %d, %d -> %d expected %d\n",opcode,x,y,num(0),result);
  CHECK(num(0) == result);
  CHECK((LispObj *)processor->sp == (LispObj *)stackcache + 2);
}
static void arithmetic(void)
{
  binary(Opcode_Add, 20, 22, 42);
  binary(Opcode_Sub, INT32_MIN, -1, INT32_MIN + 1);
  binary(Opcode_Multiply, -2147483647, 1, -2147483647);
  binary(Opcode_Quotient, -42, 7, -6);
  binary(Opcode_Quotient, 41, 2, 20);
  binary(Opcode_Quotient, -41, 2, -20);
  binary(Opcode_Quotient, 41, -2, -20);
  binary(Opcode_32BitPlus, INT32_MAX, 1, INT32_MIN);
  binary(Opcode_32BitDifference, INT32_MIN, 1, INT32_MAX);
  binary(Opcode_Ash, -1, 31, INT32_MIN);
  binary(Opcode_Ash, INT32_MIN, -32, -1);
#ifdef VLM_C_ENGINE
  /* The generated engine masks INT32_MIN to a zero shift. */
  binary(Opcode_Ash, 42, INT32_MIN, 0);
#endif
  binary(Opcode_Lsh, -1, -31, 1);
  binary(Opcode_Lsh, 42, 32, 0);
  binary(Opcode_Rot, (int32_t)0x81234567, 4, 0x12345678);
  const int values[] = {-17,-7,-5,-1,0,1,5,7,17,INT32_MAX,INT32_MIN};
  const int divisors[] = {-7,-2,2,7};
  const unsigned ops[] = {Opcode_Floor,Opcode_Ceiling,Opcode_Truncate,Opcode_Round};
  for (unsigned i=0; i<sizeof(values)/sizeof(values[0]); ++i)
    for (unsigned j=0; j<sizeof(divisors)/sizeof(divisors[0]); ++j)
      for (unsigned k=0; k<4; ++k) {
        int64_t x=values[i], y=divisors[j], q=x/y, r=x%y;
        if (k==0 && r && ((r<0)!=(y<0))) --q;
        if (k==1 && r && ((r<0)==(y<0))) ++q;
        if (k==3 && (llabs(r)*2>llabs(y) || (llabs(r)*2==llabs(y) && (q&1)))) q+=(x<0)!=(y<0)?-1:1;
        reset();fix(x);fix(y);run(halfword(ops[k],2,0));
        CHECK(num(-1)==q); CHECK(num(0)==x-q*y);
      }
  reset();fix(-1);fix(2);run(halfword(Opcode_MultiplyBignumStep,2,0));
  CHECK((uint32_t)num(-1)==0xfffffffe);CHECK(num(0)==1);
  reset();fix(-1);fix(0);fix(1);run(halfword(Opcode_AddBignumStep,2,0));
  CHECK(num(-1)==0);CHECK(num(0)==1);
}
static void host_memory_barriers(void)
{
  extern void AdjustProtection(Integer, VMAttribute);
  extern Boolean VirtualMemoryResidentScan(Integer *, Integer *, VMAttribute, int);
  const unsigned registers[] = {CoprocessorRegister_StackSwitch,
    CoprocessorRegister_FlushStackCache, CoprocessorRegister_FlipToStack};
  for (unsigned i=0; i<sizeof(registers)/sizeof(registers[0]); ++i) {
    reset();
    /* An unaligned stack spans two VM pages, as after a cache refill. GC
       clears the ephemeral bit, making the tag pages read-only. */
    Integer base=0xf8001ff0;
    EnsureVirtualAddressRange(base,512,FALSE);
    processor->stackcachebasevma=base;
    processor->stackcachetopvma=base+processor->stackcachesize;
    for (int n=0;n<40;++n) fix(100+n);
    push(Type_Locative,data+128);
    for (Integer page=base>>MemoryPage_AddressShift;
         page<=(base+41)>>MemoryPage_AddressShift; ++page)
      AdjustProtection(page<<MemoryPage_AddressShift,
        VMAttributeTable[page]&~(VMAttribute_Ephemeral|VMAttribute_Modified));
    pair(code,immediate(Opcode_CoprocessorWrite,registers[i]),halfword(Opcode_Halt,0,0));
    CHECK(InstructionSequencer()==HaltReason_Halted);
    for (int n=0;n<40;++n)
      CHECK(VirtualMemoryReadUncached(base+2+n)==MakeLispObj(Type_Fixnum,100+n));
    for (Integer page=base>>MemoryPage_AddressShift;
         page<=(base+41)>>MemoryPage_AddressShift; ++page) {
      CHECK(VMModified(VMAttributeTable[page]));
      CHECK(VMEphemeral(VMAttributeTable[page]));
    }
    CHECK(processor->stackcachebasevma==
      (registers[i]==CoprocessorRegister_FlushStackCache ? base : data+128));
  }
  /* Host VM fills also write protected tag pages. Re-protect the same page
     repeatedly, as successive collections do, and check GC page tracking. */
  for (unsigned iteration=0;iteration<12;++iteration) {
    reset();
    Integer address=data+512, page=address>>MemoryPage_AddressShift;
    VM.AddressRegister=address;
    VM.ExtentRegister=32;
    VM.DataRegister=MakeLispObj(Type_Fixnum,42+iteration);
    AdjustProtection(address,VMAttributeTable[page]&~VMAttribute_Ephemeral);
    fix(VMOpcodeFill<<19);
    pair(code,immediate(Opcode_CoprocessorWrite,CoprocessorRegister_VMRegisterCommand),
         halfword(Opcode_Halt,0,0));
    CHECK(InstructionSequencer()==HaltReason_Halted);
    CHECK(VMEphemeral(VMAttributeTable[page]));
    CHECK(VirtualMemoryReadUncached(address)==VM.DataRegister);
    CHECK(VirtualMemoryReadUncached(address+31)==VM.DataRegister);
    Integer found,count;
    int recorded=0;
    while (VirtualMemoryResidentScan(&found,&count,VMAttribute_Ephemeral,1))
      if ((found>>MemoryPage_AddressShift)==page) recorded=1;
    CHECK(recorded);
  }
}
static void addressing_and_cache(void)
{
  reset();fix(20);run(halfword(Opcode_Add,3,22));CHECK(num(0)==42);
  reset();fix(21);run(halfword(Opcode_Add,0,2));CHECK(num(0)==42);
  reset();fix(21);run(halfword(Opcode_Add,1,0));CHECK(num(0)==42);
  reset();fix(21);run(halfword(Opcode_Add,2,255));CHECK(num(0)==42);
  reset();fix(0);
  pair(code,immediate(Opcode_CoprocessorWrite,642),halfword(Opcode_Push,3,42));
  pair(code+1,halfword(Opcode_Halt,0,0),halfword(Opcode_NoOp,0,0));
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);CHECK(num(0)==42);
  const unsigned bar_tags[]={Type_Locative,Type_Fixnum,Type_Instance,Type_List};
  for (unsigned bar=0;bar<4;++bar) for (unsigned t=0;t<4;++t) {
    /* BARs hold addresses, not tagged objects. Trap handlers save them as
       locatives so GC can relocate even an interior address correctly. */
    reset();push(bar_tags[t],data+bar);
    pair(code,immediate(Opcode_WriteInternalRegister,6+bar*128),immediate(Opcode_ReadInternalRegister,6+bar*128));
    pair(code+1,halfword(Opcode_Halt,0,0),0);
    CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
    CHECK(word(tos(0))==data+bar);
    CHECK((tag(tos(0))&63)==Type_Locative);
  }
  /* The FEP reads these hardware paging registers while saving Lisp state
     for HALT GENERA. The VLM uses host paging and reports zero for each. */
  const unsigned paging_registers[]={InternalRegister_PHTBase,
    InternalRegister_PHTMask,InternalRegister_CountMapReloads};
  for (unsigned r=0;r<3;++r) {
    reset();fix(42);
    run(immediate(Opcode_ReadInternalRegister,paging_registers[r]));
    CHECK((LispObj *)processor->sp==(LispObj *)stackcache+3);
    CHECK(word(tos(0))==0);
    CHECK(tag(tos(0))==(r==0 ? Type_Locative : Type_Fixnum));
    CHECK(num(-1)==42);
  }
  /* A context switch leaves stale words beyond the valid cache extent. */
  reset();
  Integer future=processor->stackcachebasevma+400;
  VirtualMemoryWriteUncached(future,MakeLispObj(Type_Fixnum,42));
  ((LispObj *)stackcache)[400]=MakeLispObj(Type_Fixnum,17);
  push(Type_Locative,future);fix(0);
  pair(code,immediate(Opcode_CoprocessorWrite,642),immediate(Opcode_MemoryRead,16));
  pair(code+1,halfword(Opcode_Halt,0,0),0);
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
  CHECK(num(0)==42);
}
static void double_float(void)
{
  for(unsigned op=0;op<4;++op) {
    double x=3.5,y=1.25,result;
    uint64_t xb,yb,rb;
    memcpy(&xb,&x,8);memcpy(&yb,&y,8);
    reset();fix(xb>>32);fix(xb);fix(yb>>32);fix(yb);
    run(halfword(Opcode_DoubleFloatOp,3,op));
    CHECK((tag(tos(0))&63)==Type_NIL);
    rb=((uint64_t)(uint32_t)num(-2)<<32)|(uint32_t)num(-1);memcpy(&result,&rb,8);
    CHECK(result==(op==0?x+y:op==1?x-y:op==2?x*y:x/y));
  }
}
static void function_calls(void)
{
  reset();
  LispObj *s=(LispObj *)stackcache;
  s[32]=MakeLispObj(Type_Fixnum|0200,17);
  s[33]=MakeLispObj(Type_List,processor->stackcachebasevma+40);
  s[40]=MakeLispObj(Type_Fixnum|0100,42);
  push(Type_List,processor->stackcachebasevma+32);
  processor->control|=1u<<17;
  pair(code,immediate(Opcode_EntryRestNotAccepted,4),4);
  VirtualMemoryWriteUncached(code,VirtualMemoryReadUncached(code)|((LispObj)0300<<32));
  pair(code+1,halfword(Opcode_Halt,0,0),0);
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
  CHECK(num(-1)==17);CHECK(num(0)==42);
  CHECK(!(processor->control & (1u<<17)));
  CHECK((processor->control&255)==4);

  VirtualMemoryWriteUncached(data+2,MakeLispObj(Type_Symbol,data+8));
  VirtualMemoryWriteUncached(data+10,MakeLispObj(Type_CompiledFunction,code+16));
  reset();push(Type_Symbol,data);run(halfword(Opcode_StartCall,2,0));
  CHECK((LispObj *)processor->sp==(LispObj *)stackcache+3);
  CHECK(!(processor->control & (1u<<8)));
  CHECK(tag(processor->continuation)==Type_EvenPC);
  CHECK(word(processor->continuation)==code+16);

  VirtualMemoryWriteUncached(data+32,MakeLispObj(Type_Fixnum,42));
  VirtualMemoryWriteUncached(data+33,MakeLispObj(Type_Symbol,data));
  reset();push(Type_LexicalClosure,data+32);run(halfword(Opcode_StartCall,2,0));
  CHECK((LispObj *)processor->sp==(LispObj *)stackcache+3);
  CHECK(!(processor->control & (1u<<8)));
  CHECK(word(processor->continuation)==code+16);

  for(unsigned instance=0;instance<2;++instance) {
    reset();push(instance?Type_Instance:Type_GenericFunction,data);
    run(halfword(Opcode_StartCall,2,0));
    CHECK((LispObj *)processor->sp==(LispObj *)stackcache+4);
    CHECK(processor->control & (1u<<8));
    CHECK(word(tos(0))==data);
    CHECK(word(processor->continuation)==0xf8040000+(instance?05116:05114));
  }
}
static void bindings(void)
{
  reset();
  processor->bindingstackpointer=MakeLispObj(Type_Locative,data+128);
  processor->bindingstacklimit=data+256;
  VirtualMemoryWriteUncached(data,MakeLispObj(Type_Fixnum,17));
  push(Type_Locative,data);fix(42);
  pair(code,halfword(Opcode_BindLocativeToValue,2,0),halfword(Opcode_Push,3,1));
  pair(code+1,halfword(Opcode_UnbindN,2,0),halfword(Opcode_Halt,0,0));
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
  CHECK(word(VirtualMemoryReadUncached(data))==17);
  CHECK(word(processor->bindingstackpointer)==data+128);
  CHECK(!(processor->control & (1u<<25)));
}
static void instance_and_cleanup(void)
{
  VirtualMemoryWriteUncached(data,MakeLispObj(Type_HeaderP,data+32));
  VirtualMemoryWriteUncached(data+31,MakeLispObj(Type_Fixnum,3));
  VirtualMemoryWriteUncached(data+1,MakeLispObj(Type_Fixnum|0100,17));
  reset();fix(42);push(Type_Instance,data);
  run(halfword(Opcode_InstanceSet,3,1));
  LispObj value=VirtualMemoryReadUncached(data+1);
  CHECK(word(value)==42);CHECK(tag(value)==(Type_Fixnum|0100));
  reset();push(Type_Instance,data);run(halfword(Opcode_InstanceRef,3,1));CHECK(num(0)==42);
  reset();push(Type_Instance,data);run(halfword(Opcode_InstanceLoc,3,1));
  CHECK(tag(tos(0))==Type_Locative);CHECK(word(tos(0))==data+1);

  reset();processor->bindingstackpointer=MakeLispObj(Type_Locative,data+128);processor->bindingstacklimit=data+256;
  processor->catchblock=MakeLispObj(Type_NIL,Address_NIL);
  push(Type_EvenPC,code+8);
  pair(code,immediate(Opcode_CatchOpen,0),halfword(Opcode_Push,0,6));
  /* The pushed locative is a local above the catch frame. */
  ((LispObj *)stackcache)[6]=MakeLispObj(Type_Locative,data+1);
  pair(code+1,halfword(Opcode_BindLocativeToValue,3,7),immediate(Opcode_CatchClose,0));
  pair(code+2,halfword(Opcode_Halt,0,0),0);
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
  CHECK(word(processor->bindingstackpointer)==data+128);
  CHECK(!(processor->control & ((1u<<25)|(1u<<26))));
  CHECK(word(VirtualMemoryReadUncached(data+1))==42);

  /* A stack switch can set the cache base three words above the caller. */
  reset();processor->stackcachebasevma=data+5;
  processor->stackcachetopvma=data+5+processor->stackcachesize;
  processor->control=2|(3u<<9)|(3u<<30);
  processor->continuation=MakeLispObj(Type_OddPC,code);
  ((LispObj *)stackcache)[0]=MakeLispObj(Type_NIL|0300,Address_NIL);
  ((LispObj *)stackcache)[1]=MakeLispObj(Type_Fixnum|0300,2|(3u<<30));
  run(halfword(Opcode_ReturnKludge,3,0));
  CHECK(processor->stackcachebasevma==data+2);
  CHECK(processor->fp==(intptr_t)stackcache);
}
static void single_float(void)
{
#ifdef VLM_C_ENGINE
  float x=3.5f;uint32_t bits;memcpy(&bits,&x,4);
  reset();push(Type_SingleFloat,bits);fix(2);run(halfword(Opcode_Add,2,0));
  CHECK(tag(tos(0))==Type_SingleFloat);bits=word(tos(0));memcpy(&x,&bits,4);CHECK(x==5.5f);
  reset();push(Type_SingleFloat,bits);run(halfword(Opcode_Lessp,3,6));CHECK(tag(tos(0))==Type_Symbol);
#endif
}
static void tail_return_cleanup(void)
{
  for (unsigned multiple=0;multiple<2;++multiple) {
    reset();
    LispObj *s=(LispObj *)stackcache;
    const unsigned caller=2|(4u<<9)|(1u<<18)|(1u<<26)|(3u<<30);
    s[4]=MakeLispObj(Type_NIL|0300,Address_NIL);
    s[5]=MakeLispObj(Type_Fixnum|0300,2|(3u<<30));
    s[6]=MakeLispObj(Type_EvenPC,code+1);
    s[7]=MakeLispObj(Type_Locative|0100,data+128);
    s[8]=MakeLispObj(Type_NIL,Address_NIL);
    s[12]=MakeLispObj(Type_EvenPC|0300,code+2);
    s[13]=MakeLispObj(Type_Fixnum|0300,caller);
    s[14]=MakeLispObj(Type_Fixnum,42);
    s[15]=MakeLispObj(Type_Fixnum,1);
    processor->fp=(intptr_t)(s+12);
    processor->lp=(intptr_t)(s+14);
    processor->sp=(intptr_t)(s+14+multiple);
    processor->control=2|(8u<<9)|(2u<<18)|(3u<<30);
    processor->continuation=MakeLispObj(Type_EvenPC,code+3);
    processor->catchblock=MakeLispObj(Type_Locative,processor->stackcachebasevma+6);
    processor->bindingstackpointer=MakeLispObj(Type_Locative,data+128);
    pair(code+1,halfword(Opcode_Halt,0,0),0);
    pair(code+2,halfword(Opcode_Halt,0,0),0);
    run(multiple?halfword(Opcode_ReturnMultiple,2,0):immediate(Opcode_ReturnSingle,01000));
    CHECK(processor->epc==((uint64_t)(code+1)<<1));
    CHECK(processor->sp==(intptr_t)(s+13+multiple));
    CHECK(processor->fp==(intptr_t)(s+4));
    CHECK((tag(tos(0))&0300)==0200);
    CHECK(word(tos(0))==code);
    CHECK(processor->control & (1u<<23));
    pair(code+1,halfword(Opcode_Jump,2,0),0);
    flushicache();
    CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
    CHECK(processor->epc==((uint64_t)(code+2)<<1));
    CHECK(num(0)==42);
    CHECK(processor->sp==(intptr_t)(s+4));
  }
}
static void traps(void)
{
  const Integer vectors=0xf8040000, handler=code+64;
  EnsureVirtualAddressRange(vectors,8192,FALSE);
  for(unsigned i=0;i<8192;++i)
    VirtualMemoryWriteUncached(vectors+i,MakeLispObj(Type_EvenPC|0300,handler));
  reset();processor->control=2;
  pair(handler,halfword(Opcode_Halt,0,0),0);
#ifdef VLM_C_ENGINE
  /* Real faults in a host VM operation must enter the C engine's guest trap
     path; there is no generated DECODEFAULT entry point to jump to. */
  extern void AdjustProtection(Integer, VMAttribute);
  const unsigned fault_bits[]={VMAttribute_AccessFault,VMAttribute_WriteFault,
    VMAttribute_TransportFault};
  const unsigned fault_vectors[]={05121,05122,05106};
  for (unsigned f=0;f<3;++f) {
    reset();processor->control=2;
    VM.AddressRegister=data+512;VM.ExtentRegister=1;
    VM.DataRegister=MakeLispObj(Type_Fixnum,42);
    VMAttribute original=VMAttributeTable[data>>MemoryPage_AddressShift];
    AdjustProtection(data,(original&~VMAttribute_TransportDisable)|fault_bits[f]);
    fix(VMOpcodeFill<<19);
    pair(code,immediate(Opcode_CoprocessorWrite,CoprocessorRegister_VMRegisterCommand),
         halfword(Opcode_Halt,0,0));
    CHECK(InstructionSequencer()==HaltReason_Halted);
    CHECK((processor->epc>>1)==handler);
    LispObj *fault_frame=(LispObj *)processor->fp;
    CHECK(word(fault_frame[2])==fault_vectors[f]);
    CHECK(word(fault_frame[4])==data+512);
    AdjustProtection(data,original);
  }
  reset();processor->control=2;
#endif
  fix(INT32_MAX);fix(1);
  run(halfword(Opcode_Add,2,0));
  CHECK((processor->epc>>1)==handler);
  LispObj *frame=(LispObj *)processor->fp;
  CHECK(word(frame[2])==0); /* ADD fixnum/fixnum arithmetic vector. */
  CHECK((tag(frame[3])&63)==Type_EvenPC);
  CHECK(word(frame[3])==code);
  CHECK((int32_t)word(frame[4])==INT32_MAX);
  CHECK(word(frame[5])==1);
  /* An arithmetic handler's return value replaces its operands. */
  reset();fix(INT32_MAX);fix(1);
  pair(handler,halfword(Opcode_Push,3,42),immediate(Opcode_ReturnSingle,01000));
  run(halfword(Opcode_Add,2,0));
  CHECK(num(0)==42);
  CHECK((LispObj *)processor->sp==(LispObj *)stackcache+2);

  /* CHECK-PREEMPT-REQUEST promotes the deferred bit and traps at the next PC. */
  reset();processor->control=2;processor->interruptreg=2;
  pair(handler,halfword(Opcode_Halt,0,0),0);
  pair(code,immediate(Opcode_CheckPreemptRequest,0),immediate(Opcode_Branch,1));
  pair(code+1,halfword(Opcode_Halt,0,0),0);
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
  CHECK((processor->epc>>1)==handler);
  CHECK((processor->interruptreg&3)==3);
  frame=(LispObj *)processor->fp;
  CHECK(word(frame[2])==05105);
  CHECK((tag(frame[3])&62)==Type_EvenPC);
  CHECK(word(frame[3])>=code && word(frame[3])<=code+1);

  /* FINISH-CALL traps after installing the callee, then resumes its entry. */
  reset();processor->control=2;
  processor->cslimit=processor->stackcachebasevma+3;
  push(Type_CompiledFunction,code+16);
  pair(code,halfword(Opcode_StartCall,2,0),immediate(Opcode_FinishCallN,0401));
  VirtualMemoryWriteUncached(code+1,MakeLispObj(Type_Fixnum|0300,2|(3u<<30)));
  pair(code+2,immediate(Opcode_WriteInternalRegister,014),halfword(Opcode_Halt,0,0));
  pair(code+16,immediate(Opcode_EntryRestNotAccepted,2),2);
  VirtualMemoryWriteUncached(code+16,VirtualMemoryReadUncached(code+16)|((LispObj)0300<<32));
  pair(code+17,halfword(Opcode_Push,3,42),immediate(Opcode_ReturnSingle,01000));
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
  CHECK((processor->epc>>1)==handler);
  frame=(LispObj *)processor->fp;
  CHECK(word(frame[2])==05103);
  CHECK(word(frame[3])==code+16);
  CHECK(word(processor->continuation)==code+16);
  CHECK((processor->control&255)==4);
  /* Simulate the guest's growth handler and return without adding a value. */
  VirtualMemoryWriteUncached(handler,MakeLispObj(Type_Locative|0300,processor->stackcachebasevma+0x1000));
  pair(handler+1,immediate(Opcode_WriteInternalRegister,01004),halfword(Opcode_ReturnKludge,3,0));
  flushicache();
  CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
  CHECK(processor->epc==(((uint64_t)(code+2)<<1)|1));
  CHECK(num(0)==42);

  /* The guest exit-trap handler distinguishes return cleanup by microstate. */
  reset();processor->control=2|(1u<<24);
  pair(handler,halfword(Opcode_Halt,0,0),0);
  run(immediate(Opcode_ReturnSingle,01040));
  CHECK((processor->epc>>1)==handler);
  frame=(LispObj *)processor->fp;
  CHECK(word(frame[2])==05100);
  CHECK(word(frame[4])==79);
}

#ifdef VLM_C_ENGINE
static void cleanup_fetch_fault(void)
{
  extern void AdjustProtection(Integer, VMAttribute);
  const Integer cleanup=data+1024, handler=code+64, vectors=0xf8040000;
  const unsigned faults[]={VMAttribute_AccessFault,VMAttribute_TransportFault};
  const unsigned trap_vectors[]={05121,05106};
  EnsureVirtualAddressRange(vectors,8192,FALSE);
  for (unsigned fault=0;fault<2;++fault)
  for (unsigned nil=0;nil<2;++nil) {
    reset();
    LispObj *s=(LispObj *)stackcache;
    /* A tail return enters the caller's unwind-protect cleanup. Fetching
       that cleanup faults after the emulator has pushed its return PC. */
    const unsigned caller=2|(4u<<9)|(1u<<18)|(1u<<26);
    s[4]=MakeLispObj(Type_NIL|0300,Address_NIL);
    s[5]=MakeLispObj(Type_Fixnum|0300,2|(3u<<30));
    s[6]=MakeLispObj(Type_EvenPC,cleanup);
    s[7]=MakeLispObj(Type_Locative|0100,data+128);
    s[8]=MakeLispObj(Type_NIL,Address_NIL);
    s[12]=MakeLispObj(Type_EvenPC|0300,code+2);
    s[13]=MakeLispObj(Type_Fixnum|0300,caller);
    s[14]=MakeLispObj(Type_Fixnum,42);
    processor->fp=(intptr_t)(s+12);
    processor->lp=processor->sp=(intptr_t)(s+14);
    processor->control=2|(8u<<9)|(2u<<18);
    processor->continuation=MakeLispObj(Type_EvenPC,code+3);
    processor->catchblock=MakeLispObj(Type_Locative,processor->stackcachebasevma+6);
    processor->bindingstackpointer=MakeLispObj(Type_Locative,data+128);
    pair(code,immediate(Opcode_ReturnSingle,nil?01040:01000),0);
    pair(code+2,halfword(Opcode_Halt,0,0),0);
    pair(cleanup,halfword(Opcode_Jump,2,0),0);
    pair(handler,halfword(Opcode_Halt,0,0),0);
    VirtualMemoryWriteUncached(vectors+trap_vectors[fault],MakeLispObj(Type_EvenPC|0300,handler));
    VMAttribute original=VMAttributeTable[cleanup>>MemoryPage_AddressShift];
    AdjustProtection(cleanup,(original&~VMAttribute_TransportDisable)|faults[fault]);
    int reason=InstructionSequencer();
    AdjustProtection(cleanup,original);
    CHECK(reason==HaltReason_Halted);
    CHECK((processor->epc>>1)==handler);
    LispObj *frame=(LispObj *)processor->fp;
    CHECK(word(frame[2])==trap_vectors[fault]);
    CHECK(word(frame[3])==cleanup);
    CHECK(frame==s+14-nil);
    CHECK((tag(frame[-1])&63)==Type_EvenPC);
    CHECK(word(frame[-1])==code);
    if (!nil) CHECK(word(frame[-2])==42);
    /* Resume the faulted fetch without returning a value. Cleanup must
       still jump to RETURN-SINGLE and deliver the original return value. */
    pair(handler,halfword(Opcode_ReturnKludge,3,0),0);
    flushicache();
    CHECK(iInterpret((PROCESSORSTATEP)MapVirtualAddressTag(0))==HaltReason_Halted);
    CHECK(processor->epc==((uint64_t)(code+2)<<1));
    if (nil) CHECK((tag(tos(0))&63)==Type_NIL);
    else CHECK(num(0)==42);
    CHECK(processor->sp==(intptr_t)(s+4));
    CHECK(!(processor->control&(1u<<23)));
  }
}
#endif
int main(int argc, char **argv)
{
  InitializeIvoryProcessor(MapVirtualAddressData(0), MapVirtualAddressTag(0));
  Trace = argc > 1;
  EnsureVirtualAddressRange(code,16384,FALSE);
  puts("arithmetic");fflush(stdout);arithmetic();
  puts("host_memory_barriers");fflush(stdout);host_memory_barriers();
  puts("addressing_and_cache");fflush(stdout);addressing_and_cache();
  puts("double_float");fflush(stdout);double_float();
  puts("function_calls");fflush(stdout);function_calls();
  puts("single_float");fflush(stdout);single_float();
  puts("bindings");fflush(stdout);bindings();
  puts("instance_and_cleanup");fflush(stdout);instance_and_cleanup();
  puts("tail_return_cleanup");fflush(stdout);tail_return_cleanup();
  puts("traps");fflush(stdout);traps();
#ifdef VLM_C_ENGINE
  puts("cleanup_fetch_fault");fflush(stdout);cleanup_fetch_fault();
#endif
  printf("Ivory CPU tests passed (%u checks)\n",checks);
  return 0;
}
