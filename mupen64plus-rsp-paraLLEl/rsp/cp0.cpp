#include "../state.hpp"

#ifdef PARALLEL_INTEGRATION
#include "../rsp_1.1.h"
#include "m64p_plugin.h"
namespace RSP
{
extern RSP_INFO rsp;
extern short MFC0_count[32];
extern int SP_STATUS_TIMEOUT;
} // namespace RSP
#endif

using namespace RSP;

extern "C"
{

#ifdef INTENSE_DEBUG
	void log_rsp_mem_parallel(void);
#endif

	int RSP_MFC0(RSP::CPUState *rsp, unsigned rt, unsigned rd)
	{
		rd &= 15;
		uint32_t res = *rsp->cp0.cr[rd];
		if (rt)
			rsp->sr[rt] = res;

#ifdef PARALLEL_INTEGRATION
		if (rd == CP0_REGISTER_SP_STATUS)
		{
			// Might be waiting for the CPU to set a signal bit on the STATUS register. Increment timeout
			RSP::MFC0_count[rt] += 1;
			if (RSP::MFC0_count[rt] >= RSP::SP_STATUS_TIMEOUT)
			{
				*RSP::rsp.SP_STATUS_REG |= SP_STATUS_HALT;
				return MODE_CHECK_FLAGS;
			}
		}
		else if (rd == CP0_REGISTER_CMD_STATUS ||
		         rd == CP0_REGISTER_CMD_CLOCK ||
		         rd == CP0_REGISTER_CMD_BUSY ||
		         rd == CP0_REGISTER_CMD_PIPE_BUSY)
		{
			/* A microcode can spin reading the RDP (DPC) busy registers while
			 * it waits for the render command stream to drain. The RDP runs
			 * asynchronously on its own thread here, and at content close it
			 * stops advancing these registers, so the poll would never observe
			 * the not-busy state and the RSP run loop would never exit -- the
			 * emulation thread hangs inside this function. Unlike SP_STATUS,
			 * these reads had no timeout. Bound them the same way: after
			 * SP_STATUS_TIMEOUT consecutive reads, force the RSP to halt so the
			 * run loop terminates. (The counter is reset at the top of each
			 * DoRspCycles task, so this only fires on a genuine stuck spin.) */
			RSP::MFC0_count[rt] += 1;
			if (RSP::MFC0_count[rt] >= RSP::SP_STATUS_TIMEOUT)
			{
				*RSP::rsp.SP_STATUS_REG |= SP_STATUS_HALT;
				return MODE_CHECK_FLAGS;
			}
		}
		else if (rd == CP0_REGISTER_SP_SEMAPHORE)
		{
			/* Test-and-set on read, matching real hardware and the cxd4
			 * interpreter (su.c: SP_CP0_MF, rd == 0x7). The core's
			 * read_rsp_regs() only applies the set-on-read side effect to
			 * CPU-bus reads of 0x04040018; the RSP's own MFC0 path shares
			 * the same register word (cr[0x7] aliases SP_SEMAPHORE_REG,
			 * see parallelRSPInitiateRSP) but bypasses the core entirely,
			 * so we must apply it here or RSP-side lock acquisition never
			 * marks the semaphore held. Leaving it out breaks the mutex:
			 * the RSP reads 0 and "acquires", but the CPU then also reads 0
			 * and acquires too.
			 *
			 * Then halt so the RSP yields to the CPU at the acquire point.
			 * This is the fine-grained CPU<->RSP handshake that Gauntlet
			 * Legends, World Driver Championship, Stunt Racer 64 and Top
			 * Gear Rally depend on; without it the RSP runs its whole task
			 * before the CPU ever gets a turn and the handshake deadlocks.
			 * The outer run loop's "semaphore != 0" branch (parallel.cpp)
			 * keeps the SP_STATUS timeout slow while the lock is held --
			 * that branch is a no-op unless this half sets the register,
			 * which is exactly the pairing cxd4's module.c uses. */
			*rsp->cp0.cr[CP0_REGISTER_SP_SEMAPHORE] = 1;
			*RSP::rsp.SP_STATUS_REG |= SP_STATUS_HALT;
			return MODE_CHECK_FLAGS;
		}
#endif

		//if (rd == 4) // SP_STATUS_REG
		//   fprintf(stderr, "READING STATUS REG!\n");

		return MODE_CONTINUE;
	}

#define RSP_HANDLE_STATUS_WRITE(flag) \
	switch (rt & (SP_SET_##flag | SP_CLR_##flag)) \
	{ \
		case SP_SET_##flag: status |= SP_STATUS_##flag; break; \
		case SP_CLR_##flag: status &= ~SP_STATUS_##flag; break; \
		default: break; \
	}

	static inline int rsp_status_write(RSP::CPUState *rsp, uint32_t rt)
	{
		//fprintf(stderr, "Writing 0x%x to status reg!\n", rt);

		uint32_t status = *rsp->cp0.cr[CP0_REGISTER_SP_STATUS];

		RSP_HANDLE_STATUS_WRITE(HALT)
		RSP_HANDLE_STATUS_WRITE(SSTEP)
		RSP_HANDLE_STATUS_WRITE(INTR_BREAK)
		RSP_HANDLE_STATUS_WRITE(SIG0)
		RSP_HANDLE_STATUS_WRITE(SIG1)
		RSP_HANDLE_STATUS_WRITE(SIG2)
		RSP_HANDLE_STATUS_WRITE(SIG3)
		RSP_HANDLE_STATUS_WRITE(SIG4)
		RSP_HANDLE_STATUS_WRITE(SIG5)
		RSP_HANDLE_STATUS_WRITE(SIG6)
		RSP_HANDLE_STATUS_WRITE(SIG7)

		switch (rt & (SP_SET_INTR | SP_CLR_INTR))
		{
			case SP_SET_INTR: *rsp->cp0.irq |= 1; break;
			case SP_CLR_INTR: *rsp->cp0.irq &= ~1; break;
			default: break;
		}

		if (rt & SP_CLR_BROKE)
			status &= ~SP_STATUS_BROKE;

		*rsp->cp0.cr[CP0_REGISTER_SP_STATUS] = status;
		return ((*rsp->cp0.irq & 1) || (status & SP_STATUS_HALT)) ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

#ifdef PARALLEL_INTEGRATION
	/* A transfer stays inside the 4 KiB bank bit 12 of the memory address
	 * picks, wrapping there; the skip falls between rows, not after the
	 * last one. Both length registers then read back the skip, a zero count
	 * and a 0xff8 length. */
	static void rsp_dma_finish(RSP::CPUState *rsp, uint32_t length_reg, uint32_t bank, uint32_t mem,
	                           uint32_t dram)
	{
		*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = bank | (mem & 0xff8);
		*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = dram & 0xfffff8;
		*rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH] = (length_reg & 0xff800000u) | 0xff8;
		*rsp->cp0.cr[CP0_REGISTER_DMA_WRITE_LENGTH] = (length_reg & 0xff800000u) | 0xff8;
	}

	static int rsp_dma_read(RSP::CPUState *rsp)
	{
		uint32_t length_reg = *rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH];
		uint32_t length = ((length_reg & 0xFFF) | 7) + 1;
		uint32_t skip = (length_reg >> 20) & 0xFF8;
		unsigned count = ((length_reg >> 12) & 0xFF) + 1;
		uint32_t bank = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0x1000;
		uint32_t mem = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xff8;
		uint32_t dram = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] & 0xfffff8;
		unsigned i, j;

#ifdef INTENSE_DEBUG
		fprintf(stderr, "DMA READ: (0x%x <- 0x%x) len %u, count %u, skip %u\n", bank | mem, dram,
		        length, count, skip);
#endif

		for (i = 0; i < count; i++)
		{
			if (i != 0)
				dram += skip;
			for (j = 0; j < length; j += 4)
			{
				uint32_t word = rsp->rdram[(dram & 0x7FFFFC) >> 2];
				uint32_t off = mem & 0xffc;

				if (bank)
				{
					/* invalidate IMEM */
					unsigned block = off / CODE_BLOCK_SIZE;
					rsp->dirty_blocks |= (0x3 << block) >> 1;
					rsp->imem[off >> 2] = word;
				}
				else
					rsp->dmem[off >> 2] = word;

				mem = (mem + 4) & 0xfff;
				dram += 4;
			}
		}

		rsp_dma_finish(rsp, length_reg, bank, mem, dram);

#ifdef INTENSE_DEBUG
		log_rsp_mem_parallel();
#endif
		return rsp->dirty_blocks ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

	static void rsp_dma_write(RSP::CPUState *rsp)
	{
		uint32_t length_reg = *rsp->cp0.cr[CP0_REGISTER_DMA_WRITE_LENGTH];
		uint32_t length = ((length_reg & 0xFFF) | 7) + 1;
		uint32_t skip = (length_reg >> 20) & 0xFF8;
		unsigned count = ((length_reg >> 12) & 0xFF) + 1;
		uint32_t bank = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0x1000;
		uint32_t mem = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xff8;
		uint32_t dram = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] & 0xfffff8;
		unsigned i, j;

#ifdef INTENSE_DEBUG
		fprintf(stderr, "DMA WRITE: (0x%x <- 0x%x) len %u, count %u, skip %u\n", dram, bank | mem,
		        length, count, skip);
#endif

		for (i = 0; i < count; i++)
		{
			if (i != 0)
				dram += skip;
			for (j = 0; j < length; j += 4)
			{
				uint32_t off = mem & 0xffc;
				rsp->rdram[(dram & 0x7FFFFC) >> 2] = bank ? rsp->imem[off >> 2] : rsp->dmem[off >> 2];
				mem = (mem + 4) & 0xfff;
				dram += 4;
			}
		}

		rsp_dma_finish(rsp, length_reg, bank, mem, dram);
#ifdef INTENSE_DEBUG
		log_rsp_mem_parallel();
#endif
	}

#endif

	int RSP_MTC0(RSP::CPUState *rsp, unsigned rd, unsigned rt)
	{
		uint32_t val = rsp->sr[rt];

		switch (static_cast<CP0Registers>(rd & 15))
		{
		case CP0_REGISTER_DMA_CACHE:
			*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = val & 0x1ff8;
			break;

		case CP0_REGISTER_DMA_DRAM:
			*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = val & 0xfffff8;
			break;

		case CP0_REGISTER_DMA_READ_LENGTH:
			*rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH] = val;
#ifdef PARALLEL_INTEGRATION
			return rsp_dma_read(rsp);
#else
			return MODE_DMA_READ;
#endif

		case CP0_REGISTER_DMA_WRITE_LENGTH:
			*rsp->cp0.cr[CP0_REGISTER_DMA_WRITE_LENGTH] = val;
#ifdef PARALLEL_INTEGRATION
			rsp_dma_write(rsp);
#endif
			break;

		case CP0_REGISTER_SP_STATUS:
			return rsp_status_write(rsp, val);

		case CP0_REGISTER_SP_SEMAPHORE:
			// Any write to the semaphore register, regardless of value, sets it to 0 for the next read
			*rsp->cp0.cr[CP0_REGISTER_SP_SEMAPHORE] = 0;
			break;

		case CP0_REGISTER_CMD_START:
#ifdef INTENSE_DEBUG
			fprintf(stderr, "CMD_START 0x%x\n", val & 0xfffffff8u);
#endif
			*rsp->cp0.cr[CP0_REGISTER_CMD_START] = *rsp->cp0.cr[CP0_REGISTER_CMD_CURRENT] =
			    *rsp->cp0.cr[CP0_REGISTER_CMD_END] = val & 0xfffffff8u;
			break;

		case CP0_REGISTER_CMD_END:
#ifdef INTENSE_DEBUG
			fprintf(stderr, "CMD_END 0x%x\n", val & 0xfffffff8u);
#endif
			*rsp->cp0.cr[CP0_REGISTER_CMD_END] = val & 0xfffffff8u;

#ifdef PARALLEL_INTEGRATION
			RSP::rsp.ProcessRdpList();
#endif
			break;

		case CP0_REGISTER_CMD_CLOCK:
			*rsp->cp0.cr[CP0_REGISTER_CMD_CLOCK] = val;
			break;

		case CP0_REGISTER_CMD_STATUS:
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x1) << 0);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x2) << 0);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x4) << 1);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x8) << 1);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x10) << 2);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x20) << 2);
			*rsp->cp0.cr[CP0_REGISTER_CMD_TMEM_BUSY] &= !(val & 0x40) * -1;
			*rsp->cp0.cr[CP0_REGISTER_CMD_CLOCK] &= !(val & 0x200) * -1;
			break;

		/* DMA_FULL and DMA_BUSY are read-only */
		case CP0_REGISTER_DMA_FULL:
		case CP0_REGISTER_DMA_BUSY:
		case CP0_REGISTER_CMD_CURRENT:
		case CP0_REGISTER_CMD_BUSY:
		case CP0_REGISTER_CMD_PIPE_BUSY:
		case CP0_REGISTER_CMD_TMEM_BUSY:
			break;

		default:
			*rsp->cp0.cr[rd & 15] = val;
			break;
		}

		return MODE_CONTINUE;
	}
}
