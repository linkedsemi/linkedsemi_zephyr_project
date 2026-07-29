/*
 * Copyright (c) 2024 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/atomic.h>
#include <soc.h>
#include <platform.h>
#include "smp/lsqsh_smp.h"

/* Use a timer IRQ that is normally not enabled by other drivers in this sample. */
#define TEST_IRQ    GPTIMA1_IRQN
#define TEST_PRIO   1

extern void riscv_clic_irq_set_pending(uint32_t irq);
extern void riscv_clic_irq_disable_trigger_mode(uint32_t irq);

static atomic_t isr_count[CONFIG_MP_MAX_NUM_CPUS];

static void test_isr(const void *arg)
{
	ARG_UNUSED(arg);

	uint32_t cpu = arch_curr_cpu()->id;

	if (cpu < CONFIG_MP_MAX_NUM_CPUS) {
		atomic_inc(&isr_count[cpu]);
	}

	/* CLIC software pending only works when the trigger mode is edge.
	 * Restore level-triggered mode before clearing pending, matching the
	 * UART driver pattern.
	 */
	riscv_clic_irq_disable_trigger_mode(TEST_IRQ);
	csi_vic_clear_pending_irq(TEST_IRQ);
}

static void reset_counts(void)
{
	for (int i = 0; i < CONFIG_MP_MAX_NUM_CPUS; i++) {
		atomic_set(&isr_count[i], 0);
	}
}

static K_THREAD_STACK_DEFINE(test_stack, 1024);
static struct k_thread test_thread;

static atomic_t remote_enabled;
static atomic_t remote_priority;

static void check_enabled_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	atomic_set(&remote_enabled, irq_is_enabled(TEST_IRQ) ? 1 : 0);
}

static int check_enabled_on_cpu(int cpu)
{
	k_tid_t tid = k_thread_create(&test_thread, test_stack,
				      K_THREAD_STACK_SIZEOF(test_stack),
				      check_enabled_thread_fn, NULL, NULL, NULL,
				      K_PRIO_PREEMPT(0), 0, K_FOREVER);

	(void)k_thread_cpu_pin(tid, cpu);
	k_thread_start(tid);
	(void)k_thread_join(&test_thread, K_MSEC(200));

	return (int)atomic_get(&remote_enabled);
}

static void clear_pending_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* The CLIC pending bit set by riscv_clic_irq_set_pending() is edge
	 * triggered; restore level mode before clearing, just like the ISR does.
	 */
	riscv_clic_irq_disable_trigger_mode(TEST_IRQ);
	csi_vic_clear_pending_irq(TEST_IRQ);
}

static void clear_pending_on_cpu(int cpu)
{
	k_tid_t tid = k_thread_create(&test_thread, test_stack,
				      K_THREAD_STACK_SIZEOF(test_stack),
				      clear_pending_thread_fn, NULL, NULL, NULL,
				      K_PRIO_PREEMPT(0), 0, K_FOREVER);

	(void)k_thread_cpu_pin(tid, cpu);
	k_thread_start(tid);
	(void)k_thread_join(&test_thread, K_MSEC(200));
}

static void read_priority_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	atomic_set(&remote_priority, CLIC->CLICINT[TEST_IRQ].CTL);
}

static int read_priority_on_cpu(int cpu)
{
	k_tid_t tid = k_thread_create(&test_thread, test_stack,
				      K_THREAD_STACK_SIZEOF(test_stack),
				      read_priority_thread_fn, NULL, NULL, NULL,
				      K_PRIO_PREEMPT(0), 0, K_FOREVER);

	(void)k_thread_cpu_pin(tid, cpu);
	k_thread_start(tid);
	(void)k_thread_join(&test_thread, K_MSEC(200));

	return (int)atomic_get(&remote_priority);
}

static void trigger_thread_fn(void *p1, void *p2, void *p3)
{
	int target_cpu = (int)(uintptr_t)p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* The thread was already pinned to the target CPU before it was started.
	 * Just sanity-check that we are really running there.
	 */
	__ASSERT(arch_curr_cpu()->id == (uint32_t)target_cpu,
		 "trigger thread not on target CPU");

	riscv_clic_irq_set_pending(TEST_IRQ);

	/* Small delay to let the ISR run before the thread exits. */
	k_busy_wait(500);
}

static void trigger_from_cpu(int cpu)
{
	k_tid_t tid = k_thread_create(&test_thread, test_stack,
				      K_THREAD_STACK_SIZEOF(test_stack),
				      trigger_thread_fn, (void *)(uintptr_t)cpu,
				      NULL, NULL,
				      K_PRIO_PREEMPT(0), 0, K_FOREVER);

	/* Pin the thread to the target CPU before starting it so that its first
	 * run happens on that CPU.
	 */
	(void)k_thread_cpu_pin(tid, cpu);
	k_thread_start(tid);

	(void)k_thread_join(&test_thread, K_MSEC(200));
}

int main(void)
{
	int failures = 0;

	printk("IRQ affinity test on IRQ %d\n", TEST_IRQ);

	IRQ_CONNECT(TEST_IRQ, TEST_PRIO, test_isr, NULL, IRQ_TYPE_LEVEL_HIGH);
	irq_enable(TEST_IRQ);

	/* Test 1: default affinity is CPU0. */
	reset_counts();
	trigger_from_cpu(0);

	if (atomic_get(&isr_count[0]) != 1 || atomic_get(&isr_count[1]) != 0) {
		printk("FAIL: default CPU0 affinity (cpu0=%d cpu1=%d)\n",
		       (int)atomic_get(&isr_count[0]), (int)atomic_get(&isr_count[1]));
		failures++;
	} else {
		printk("PASS: default CPU0 affinity\n");
	}

	/* Test 2: migrate the interrupt to CPU1. */
	if (lsqsh_clic_irq_set_affinity_sync(TEST_IRQ, BIT(1)) != 0) {
		printk("FAIL: set affinity to CPU1\n");
		failures++;
	} else {
		reset_counts();
		trigger_from_cpu(1);

		if (atomic_get(&isr_count[0]) != 0 || atomic_get(&isr_count[1]) != 1) {
			printk("FAIL: CPU1 affinity (cpu0=%d cpu1=%d)\n",
			       (int)atomic_get(&isr_count[0]), (int)atomic_get(&isr_count[1]));
			failures++;
		} else {
			printk("PASS: CPU1 affinity\n");
		}
	}

	/* Test 3: both CPUs hold the interrupt. */
	if (lsqsh_clic_irq_set_affinity_sync(TEST_IRQ, BIT(0) | BIT(1)) != 0) {
		printk("FAIL: set affinity to both CPUs\n");
		failures++;
	} else {
		reset_counts();
		trigger_from_cpu(0);
		trigger_from_cpu(1);

		if (atomic_get(&isr_count[0]) != 1 || atomic_get(&isr_count[1]) != 1) {
			printk("FAIL: both affinity (cpu0=%d cpu1=%d)\n",
			       (int)atomic_get(&isr_count[0]), (int)atomic_get(&isr_count[1]));
			failures++;
		} else {
			printk("PASS: both affinity\n");
		}
	}

	/* Test 4: repeated triggers while both CPUs hold the interrupt. */
	reset_counts();
	for (int i = 0; i < 3; i++) {
		trigger_from_cpu(0);
	}
	for (int i = 0; i < 3; i++) {
		trigger_from_cpu(1);
	}

	if (atomic_get(&isr_count[0]) != 3 || atomic_get(&isr_count[1]) != 3) {
		printk("FAIL: repeated triggers (cpu0=%d cpu1=%d)\n",
		       (int)atomic_get(&isr_count[0]), (int)atomic_get(&isr_count[1]));
		failures++;
	} else {
		printk("PASS: repeated triggers\n");
	}

	/* Test 5: migrate back to CPU0 and confirm CPU1 no longer owns it. */
	if (lsqsh_clic_irq_set_affinity_sync(TEST_IRQ, BIT(0)) != 0) {
		printk("FAIL: set affinity back to CPU0\n");
		failures++;
	} else {
		reset_counts();
		trigger_from_cpu(1);

		if (atomic_get(&isr_count[0]) != 0 || atomic_get(&isr_count[1]) != 0) {
			printk("FAIL: CPU0 affinity fired on CPU1 (cpu0=%d cpu1=%d)\n",
			       (int)atomic_get(&isr_count[0]), (int)atomic_get(&isr_count[1]));
			failures++;
		} else if (check_enabled_on_cpu(1) != 0) {
			printk("FAIL: CPU1 still sees IRQ enabled after migration to CPU0\n");
			failures++;
		} else {
			trigger_from_cpu(0);

			if (atomic_get(&isr_count[0]) != 1 || atomic_get(&isr_count[1]) != 0) {
				printk("FAIL: CPU0 affinity (cpu0=%d cpu1=%d)\n",
				       (int)atomic_get(&isr_count[0]), (int)atomic_get(&isr_count[1]));
				failures++;
			} else {
				printk("PASS: migrate back to CPU0\n");
			}
		}
	}

	/* Test 6: irq_disable() from CPU0 propagates to CPU1. */
	if (lsqsh_clic_irq_set_affinity_sync(TEST_IRQ, BIT(1)) != 0) {
		printk("FAIL: set affinity to CPU1 for disable test\n");
		failures++;
	} else {
		irq_disable(TEST_IRQ);
		reset_counts();
		trigger_from_cpu(1);

		if (atomic_get(&isr_count[0]) != 0 || atomic_get(&isr_count[1]) != 0) {
			printk("FAIL: disable did not propagate (cpu0=%d cpu1=%d)\n",
			       (int)atomic_get(&isr_count[0]), (int)atomic_get(&isr_count[1]));
			failures++;
		} else {
			printk("PASS: disable propagates to CPU1\n");
		}

		/* Re-enable and clean up the pending bit that was set while disabled. */
		irq_enable(TEST_IRQ);
		k_busy_wait(3000);
		clear_pending_on_cpu(1);
		reset_counts();
		trigger_from_cpu(1);

		if (atomic_get(&isr_count[0]) != 0 || atomic_get(&isr_count[1]) != 1) {
			printk("FAIL: re-enable did not propagate (cpu0=%d cpu1=%d)\n",
			       (int)atomic_get(&isr_count[0]), (int)atomic_get(&isr_count[1]));
			failures++;
		} else {
			printk("PASS: re-enable propagates to CPU1\n");
		}
	}

	/* Test 7: priority/trigger configuration is propagated to the target CPU. */
	{
		int raw_ctl = read_priority_on_cpu(1);
		uint32_t nlbits =
			(CLIC->CLICINFO & CLIC_INFO_CLICINTCTLBITS_Msk) >>
			CLIC_INFO_CLICINTCTLBITS_Pos;
		uint32_t actual_prio = (uint32_t)raw_ctl >> (8U - nlbits);

		if (actual_prio != TEST_PRIO) {
			printk("FAIL: priority not propagated (expected %u, got %lu)\n",
			       TEST_PRIO, (unsigned long)actual_prio);
			failures++;
		} else {
			printk("PASS: priority propagated to CPU1\n");
		}
	}

	if (failures == 0) {
		printk("All IRQ affinity tests passed\n");
	} else {
		printk("%d IRQ affinity test(s) failed\n", failures);
	}

	return 0;
}
