// v0.5.2 counts a MOVE.L (SP)+ as a discarded return slot, but does not
// subtract the two MOVE.W -(SP) that saved those same four bytes. Ice Cap's
// segmented columns and ice blocks then skip their caller's normal return pop,
// leaking four guest-stack bytes per update until the PLC queue is overwritten.
// Correct only that bookkeeping at the four verified native function entries.
// The executable, cartridge instructions, physics and actual stack writes stay
// unchanged. All addresses are covered by the adapter's executable hash gate.
static void (*paired_stack_original[4])();
static void paired_stack_0(){--g_split_sp_popped;paired_stack_original[0]();}
static void paired_stack_1(){--g_split_sp_popped;paired_stack_original[1]();}
static void paired_stack_2(){
    uint32_t parent=uint32_t(int32_t(int16_t(m68k_read16(g_cpu.A[0]+0x46))));
    // This older column routine can branch away before saving the velocities.
    if(!(m68k_read8(parent+0x38)&1))--g_split_sp_popped;
    paired_stack_original[2]();
}
static void paired_stack_3(){--g_split_sp_popped;paired_stack_original[3]();}
static bool paired_stack_hooks(){
    const unsigned rvas[]={0xeefb00,0xf0cc10,0x180d770,0x1827cc0};
    void (*fixed[])()={paired_stack_0,paired_stack_1,paired_stack_2,paired_stack_3};
    for(unsigned i=0;i<4;i++)if(!hook(oa_exe_base+rvas[i],reinterpret_cast<void*>(fixed[i]),reinterpret_cast<void**>(&paired_stack_original[i])))return false;
    return true;
}
