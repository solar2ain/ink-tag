/* Diagnostic-only RAM breadcrumb. Never a clock-trust or power-loss cookie.
 * The App scatter file reserves exactly 32 UNINIT bytes in its own retained
 * post-exchange gap, outside SDK heaps and the loader body. No Flash/heap/BLE
 * calls, watchdog changes or debugger enablement occur on a fault path.
 * Arm armlink $Sub$$/$Super$$ keeps the original SDK handlers in charge. */
#define INKTAG_DA_RESET_TRACE_SEAL 0x44525431u
typedef struct {
    uint32_t seal, reason, pc, lr, tick, timestamp, sequence, kind;
} inktag_da_reset_trace_t;
_Static_assert(sizeof(inktag_da_reset_trace_t) == 32u, "bounded reset trace");
static volatile inktag_da_reset_trace_t s_reset_trace
    __attribute__((section(".bss.inktag_reset_trace"), aligned(4), used));
/* Ordinary ZI: distinguish the NMI intentionally raised by this boot's
 * HardFault handler from a later watchdog reset after the next App boot. */
static bool s_reset_fault_pending;

/* Reuse one bounded volatile checksum scan for validation and replacement.
 * Keeping this out of line also avoids duplicating seven field loads in the
 * new startup marker. A marker is not a reset counter or clock-trust cookie. */
__attribute__((noinline)) static uint32_t reset_trace_checksum(void)
{
    const volatile uint32_t *words = (const volatile uint32_t *)&s_reset_trace;
    uint32_t seal = INKTAG_DA_RESET_TRACE_SEAL;
    for (uint32_t i = 1u; i < 8u; ++i) seal ^= words[i];
    return seal;
}

__attribute__((noinline)) static void reset_trace_record(
    uint32_t kind, uint32_t reason, uint32_t pc, uint32_t lr)
{
    uint32_t sequence = 1u;
    if (s_reset_trace.seal != 0u &&
        s_reset_trace.seal == reset_trace_checksum())
        sequence = s_reset_trace.sequence + 1u;
    s_reset_trace.seal = 0u; /* Invalidate before any replacement field. */
    s_reset_trace.reason = reason;
    s_reset_trace.pc = pc;
    s_reset_trace.lr = lr;
    s_reset_trace.tick = s_wall_clock.last_tick;
    s_reset_trace.timestamp = s_timestamp;
    s_reset_trace.sequence = sequence;
    s_reset_trace.kind = kind;
    s_reset_trace.seal = reset_trace_checksum();
}

__attribute__((noinline)) static void reset_trace_arm(void)
{
    /* Preserve any valid old fault/reset record. Otherwise establish a known
     * kind4 marker so later invalid RAM is distinguishable from never having
     * recorded a fault. No claim that the loader or power loss retains it. */
    if (s_reset_trace.seal != 0u &&
        s_reset_trace.seal == reset_trace_checksum()) return;
    reset_trace_record(4u, 0u, 0u, 0u);
}

static void reset_trace_exception(uint32_t kind, unsigned long *frame)
{
    uintptr_t address = (uintptr_t)frame;
    const uint32_t *stacked = (const uint32_t *)frame;
    uint32_t pc = 0u, lr = 0u;
    if ((address & 3u) == 0u && address >= 0x07fc0000u && address <= 0x07fd7fe0u) {
        pc = stacked[6];
        lr = stacked[5];
    }
    reset_trace_record(kind, 0u, pc, lr);
}

#if defined(INKTAG_DA_RESET_TRACE_HOST_TEST)
extern void inktag_da_super_reset(uint32_t);
extern void inktag_da_super_nmi(unsigned long *);
extern void inktag_da_super_fault(unsigned long *);
#else
extern void inktag_da_super_reset(uint32_t) __asm("$Super$$platform_reset_func");
extern void inktag_da_super_nmi(unsigned long *) __asm("$Super$$NMI_HandlerC");
extern void inktag_da_super_fault(unsigned long *) __asm("$Super$$HardFault_HandlerC");
void inktag_da_sub_reset(uint32_t) __asm("$Sub$$platform_reset_func");
void inktag_da_sub_nmi(unsigned long *) __asm("$Sub$$NMI_HandlerC");
void inktag_da_sub_fault(unsigned long *) __asm("$Sub$$HardFault_HandlerC");
#endif

__attribute__((noinline, used)) void inktag_da_sub_reset(uint32_t reason)
{
    /* Record even the SDK's ordinary reset reason: that constant identifies
     * neither the originating caller nor whether a human requested a reset.
     * Diagnostic RAM only; never promote this into a public crash count. */
    reset_trace_record(3u, reason, (uint32_t)(uintptr_t)__builtin_return_address(0), 0u);
    inktag_da_super_reset(reason);
}

__attribute__((noinline, used)) void inktag_da_sub_nmi(unsigned long *frame)
{
    if (!s_reset_fault_pending) reset_trace_exception(1u, frame);
    inktag_da_super_nmi(frame);
}

__attribute__((noinline, used)) void inktag_da_sub_fault(unsigned long *frame)
{
    reset_trace_exception(2u, frame);
    s_reset_fault_pending = true;
    inktag_da_super_fault(frame);
}
