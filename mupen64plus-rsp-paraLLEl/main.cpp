#include "rsp.hpp"
#include <vector>
#include <stdio.h>

using namespace std;

static inline uint32_t flip_endian(uint32_t v)
{
   return
      (v >> 24) |
      (v << 24) |
      ((v >> 8) & 0x0000ff00) |
      ((v << 8) & 0x00ff0000);
}

static vector<uint32_t> read_binary(const char *path, bool flip)
{
   FILE *f = fopen(path, "rb");
   if (!f)
      return {};

   fseek(f, 0, SEEK_END);
   long len = ftell(f);
   rewind(f);

   vector<uint32_t> v(len / 4);
   fread(v.data(), sizeof(uint32_t), v.size(), f);
   fclose(f);

   if (flip)
      for (auto &value : v)
         value = flip_endian(value);
   return v;
}

static bool read_tag_validate(FILE *file, const char *tag)
{
   char tmp[9] = {};
   if (fread(tmp, 1, 8, file) != 8)
      throw runtime_error("Failed to read tag.");

   if (strcmp(tmp, "EOF     ") == 0)
      return false;

   if (strcmp(tmp, tag))
      throw runtime_error("Unexpected tag.");

   return true;
}

static bool read_block(FILE *file, const char *tag, void *buffer, size_t size)
{
   if (!read_tag_validate(file, tag))
      return false;

   uint32_t block_size;
   if (fread(&block_size, sizeof(block_size), 1, file) != 1)
      throw runtime_error("EOF");

   if (size != block_size)
      throw runtime_error("Unexpected size");

   if (fread(buffer, size, 1, file) != 1)
      throw runtime_error("EOF");

   return true;
}

static bool read_poke(FILE *file, RSP::CPU &cpu)
{
   char tmp[9] = {};
   if (fread(tmp, 1, 8, file) != 8)
      throw runtime_error("Failed to read tag.");

   if (strcmp(tmp, "ENDDMA  ") == 0)
      return false;

   if (strcmp(tmp, "POKE    "))
      throw runtime_error("Unexpected tag.");

   uint32_t offset;
   uint32_t len;

   if (fread(&offset, sizeof(offset), 1, file) != 1)
      throw runtime_error("Wrong EOF");
   if (fread(&len, sizeof(len), 1, file) != 1)
      throw runtime_error("Wrong EOF");

   if (offset >= 0x1000)
   {
      if (fread(reinterpret_cast<uint8_t *>(cpu.get_state().imem) + offset - 0x1000, len, 1, file) != 1)
         throw runtime_error("Wrong EOF");
   }
   else
   {
      if (fread(reinterpret_cast<uint8_t *>(cpu.get_state().dmem) + offset, len, 1, file) != 1)
         throw runtime_error("Wrong EOF");
   }

   return true;
}

static void validate_trace(RSP::CPU &cpu, const char *path)
{
   auto &state = cpu.get_state();
   uint32_t dmem[1024];
   uint32_t imem[1024];
   cpu.set_dmem(dmem);
   cpu.set_imem(imem);

   FILE *file = fopen(path, "rb");
   if (!file)
      throw runtime_error("Failed to load trace.");

   try
   {
      read_tag_validate(file, "RSPDUMP1");

      unsigned index = 0;

      while (read_tag_validate(file, "BEGIN   "))
      {
         read_block(file, "DMEM    ", state.dmem, 0x1000);
         read_block(file, "IMEM    ", state.imem, 0x1000);
         read_block(file, "SR32    ", state.sr, sizeof(state.sr));
         read_block(file, "VR32    ", state.cp2.regs, sizeof(state.cp2.regs));
         read_block(file, "VLO     ", state.cp2.acc.e + RSP::RSP_ACC_LO, sizeof(uint16_t) * 8);
         read_block(file, "VMD     ", state.cp2.acc.e + RSP::RSP_ACC_MD, sizeof(uint16_t) * 8);
         read_block(file, "VHI     ", state.cp2.acc.e + RSP::RSP_ACC_HI, sizeof(uint16_t) * 8);
         read_block(file, "PC      ", &state.pc, sizeof(state.pc));

         int16_t VCO, VCC, VCE;
         read_block(file, "VCO     ", &VCO, sizeof(VCO));
         read_block(file, "VCC     ", &VCC, sizeof(VCC));
         read_block(file, "VCE     ", &VCE, sizeof(VCE));

         rsp_set_flags(state.cp2.flags[RSP::RSP_VCO].e, VCO);
         rsp_set_flags(state.cp2.flags[RSP::RSP_VCC].e, VCC);
         rsp_set_flags(state.cp2.flags[RSP::RSP_VCE].e, VCE);

         RSP::ReturnMode mode = RSP::MODE_CONTINUE;
         do
         {
            *state.cp0.cr[RSP::CP0_REGISTER_SP_STATUS] = 0;
            cpu.invalidate_imem();

            // Run till break.
            mode = cpu.run();
            if (mode == RSP::MODE_DMA_READ)
            {
               if (!read_tag_validate(file, "BEGINDMA"))
                  throw runtime_error("Expected BEGINDMA.");
               while (read_poke(file, cpu));
            }
         } while (mode != RSP::MODE_BREAK);

         uint32_t dmem[0x1000 >> 2];
         uint32_t imem[0x1000 >> 2];
         uint32_t sr[32];
         uint16_t vr[32 * 8];
         uint16_t vlo[8];
         uint16_t vmd[8];
         uint16_t vhi[8];

         read_block(file, "DMEM END", dmem, sizeof(dmem));
         read_block(file, "IMEM END", imem, sizeof(imem));
         read_block(file, "SR32 END", sr, sizeof(sr));
         read_block(file, "VR32 END", vr, sizeof(vr));
         read_block(file, "VLO  END", vlo, sizeof(vlo));
         read_block(file, "VMD  END", vmd, sizeof(vmd));
         read_block(file, "VHI  END", vhi, sizeof(vhi));
         read_block(file, "VCO  END", &VCO, sizeof(VCO));
         read_block(file, "VCC  END", &VCC, sizeof(VCC));
         read_block(file, "VCE  END", &VCE, sizeof(VCE));

         unsigned errors = 0;

         fprintf(stderr, "==== Trace #%u ====\n", index);

         // Validate DMEM
         for (unsigned i = 0; i < (0x1000 >> 2); i++)
         {
            if (state.dmem[i] != dmem[i])
            {
               fprintf(stderr, "DMEM32[0x%03x] fault. Expected 0x%08x, got 0x%08x!\n",
                     i, dmem[i], state.dmem[i]);
               errors++;
            }
         }

         // Validate IMEM (in case of DMA)
         for (unsigned i = 0; i < (0x1000 >> 2); i++)
         {
            if (state.imem[i] != imem[i])
            {
               fprintf(stderr, "IMEM32[0x%03x] fault. Expected 0x%08x, got 0x%08x!\n",
                     i, dmem[i], state.dmem[i]);
               errors++;
            }
         }

         // Validate SR
         for (unsigned i = 0; i < 32; i++)
         {
            if (sr[i] != state.sr[i])
            {
               fprintf(stderr, "SR[%02u] fault. Expected 0x%08x, got 0x%08x!\n",
                     i, sr[i], state.sr[i]);
               errors++;
            }
         }

         // Validate VR
         for (unsigned i = 0; i < 16 * 8; i++)
         {
            if (vr[i] != state.cp2.regs[i >> 3].e[i & 7])
            {
               fprintf(stderr, "VR[%02u][%u] fault. Expected 0x%04x, got 0x%04x!\n",
                     i >> 3, i & 7, vr[i], state.cp2.regs[i >> 3].e[i & 7]);
               errors++;
            }
         }

         // Validate VLO
         for (unsigned i = 0; i < 8; i++)
         {
            if (vlo[i] != state.cp2.acc.e[RSP::RSP_ACC_LO + i])
            {
               fprintf(stderr, "VLO[%u] fault. Expected 0x%04x, got 0x%04x!\n",
                     i, vlo[i], state.cp2.acc.e[RSP::RSP_ACC_LO + i]);
               errors++;
            }
         }

         // Validate VMD
         for (unsigned i = 0; i < 8; i++)
         {
            if (vmd[i] != state.cp2.acc.e[RSP::RSP_ACC_MD + i])
            {
               fprintf(stderr, "VMD[%u] fault. Expected 0x%04x, got 0x%04x!\n",
                     i, vmd[i], state.cp2.acc.e[RSP::RSP_ACC_MD + i]);
               errors++;
            }
         }

         // Validate VHI
         for (unsigned i = 0; i < 8; i++)
         {
            if (vhi[i] != state.cp2.acc.e[RSP::RSP_ACC_HI + i])
            {
               fprintf(stderr, "VHI[%u] fault. Expected 0x%04x, got 0x%04x!\n",
                     i, vhi[i], state.cp2.acc.e[RSP::RSP_ACC_HI + i]);
               errors++;
            }
         }

         // Validate flags
         if (VCO != rsp_get_flags(state.cp2.flags[RSP::RSP_VCO].e))
         {
            fprintf(stderr, "VCO fault. Expected 0x%04x, got 0x%04x!\n",
                  VCO, rsp_get_flags(state.cp2.flags[RSP::RSP_VCO].e));
            errors++;
         }

         if (VCC != rsp_get_flags(state.cp2.flags[RSP::RSP_VCC].e))
         {
            fprintf(stderr, "VCC fault. Expected 0x%04x, got 0x%04x!\n",
                  VCC, rsp_get_flags(state.cp2.flags[RSP::RSP_VCC].e));
            errors++;
         }

         if (VCE != rsp_get_flags(state.cp2.flags[RSP::RSP_VCE].e))
         {
            fprintf(stderr, "VCE fault. Expected 0x%04x, got 0x%04x!\n",
                  VCE, rsp_get_flags(state.cp2.flags[RSP::RSP_VCE].e));
            errors++;
         }

         read_tag_validate(file, "END     ");

         if (errors == 0)
            fprintf(stderr, "SUCCESS! :D\n");
         else
            fprintf(stderr, "%u ERRORS! :{\n", errors);
         fprintf(stderr, "======================\n\n");

         index++;
      }
   }
   catch (const std::exception &e)
   {
      fprintf(stderr, "Exception: %s\n", e.what());
   }

   fclose(file);
}

int main(int argc, char *argv[])
{
   RSP::CPU cpu;
   auto &state = cpu.get_state();

   uint32_t cr[16] = {};
   for (unsigned i = 0; i < 16; i++)
      state.cp0.cr[i] = &cr[i];

   if (argc == 3)
   {
      auto dmem = read_binary(argv[1], true);
      auto imem = read_binary(argv[2], true);
      if (imem.empty())
         return 1;

      dmem.resize(0x1000);
      imem.resize(0x1000);
      cpu.set_dmem(dmem.data());
      cpu.set_imem(imem.data());

      for (unsigned i = 0; i < 1; i++)
      {
         cpu.invalidate_imem();
         cr[RSP::CP0_REGISTER_SP_STATUS] = 0;
         cpu.run();
      }
   }
   else if (argc == 2)
      validate_trace(cpu, argv[1]);
   else
      return 1;
}
