/* RSP-exact triangle attribute setup for the angrylion HLE path.
 *
 * Transcribed from the F3DEX2 microcode triangle write (tri_to_rdp in the
 * armips disassembly) and the vertex-processing 1/w computation, using the
 * RSP vector-unit arithmetic semantics: the 48-bit per-lane accumulator,
 * the VRCP divide ROM, and the exact clamp rules of the multiply family,
 * as implemented by the cxd4 LLE RSP. The goal is bit-identical RDP
 * command words to what the microcode produces for the same vertices.
 *
 * C89, no external dependencies.
 */

#include <stdint.h>
#include "rdp_emit_rsp.h"

/* 11-bit divide result look-up table (VRCP family), from cxd4. */
static const uint16_t div_rom[1024] = {
    0xFFFFu, 0xFF00u, 0xFE01u, 0xFD04u, 0xFC07u, 0xFB0Cu, 0xFA11u, 0xF918u,
    0xF81Fu, 0xF727u, 0xF631u, 0xF53Bu, 0xF446u, 0xF352u, 0xF25Fu, 0xF16Du,
    0xF07Cu, 0xEF8Bu, 0xEE9Cu, 0xEDAEu, 0xECC0u, 0xEBD3u, 0xEAE8u, 0xE9FDu,
    0xE913u, 0xE829u, 0xE741u, 0xE65Au, 0xE573u, 0xE48Du, 0xE3A9u, 0xE2C5u,
    0xE1E1u, 0xE0FFu, 0xE01Eu, 0xDF3Du, 0xDE5Du, 0xDD7Eu, 0xDCA0u, 0xDBC2u,
    0xDAE6u, 0xDA0Au, 0xD92Fu, 0xD854u, 0xD77Bu, 0xD6A2u, 0xD5CAu, 0xD4F3u,
    0xD41Du, 0xD347u, 0xD272u, 0xD19Eu, 0xD0CBu, 0xCFF8u, 0xCF26u, 0xCE55u,
    0xCD85u, 0xCCB5u, 0xCBE6u, 0xCB18u, 0xCA4Bu, 0xC97Eu, 0xC8B2u, 0xC7E7u,
    0xC71Cu, 0xC652u, 0xC589u, 0xC4C0u, 0xC3F8u, 0xC331u, 0xC26Bu, 0xC1A5u,
    0xC0E0u, 0xC01Cu, 0xBF58u, 0xBE95u, 0xBDD2u, 0xBD10u, 0xBC4Fu, 0xBB8Fu,
    0xBACFu, 0xBA10u, 0xB951u, 0xB894u, 0xB7D6u, 0xB71Au, 0xB65Eu, 0xB5A2u,
    0xB4E8u, 0xB42Eu, 0xB374u, 0xB2BBu, 0xB203u, 0xB14Bu, 0xB094u, 0xAFDEu,
    0xAF28u, 0xAE73u, 0xADBEu, 0xAD0Au, 0xAC57u, 0xABA4u, 0xAAF1u, 0xAA40u,
    0xA98Eu, 0xA8DEu, 0xA82Eu, 0xA77Eu, 0xA6D0u, 0xA621u, 0xA574u, 0xA4C6u,
    0xA41Au, 0xA36Eu, 0xA2C2u, 0xA217u, 0xA16Du, 0xA0C3u, 0xA01Au, 0x9F71u,
    0x9EC8u, 0x9E21u, 0x9D79u, 0x9CD3u, 0x9C2Du, 0x9B87u, 0x9AE2u, 0x9A3Du,
    0x9999u, 0x98F6u, 0x9852u, 0x97B0u, 0x970Eu, 0x966Cu, 0x95CBu, 0x952Bu,
    0x948Bu, 0x93EBu, 0x934Cu, 0x92ADu, 0x920Fu, 0x9172u, 0x90D4u, 0x9038u,
    0x8F9Cu, 0x8F00u, 0x8E65u, 0x8DCAu, 0x8D30u, 0x8C96u, 0x8BFCu, 0x8B64u,
    0x8ACBu, 0x8A33u, 0x899Cu, 0x8904u, 0x886Eu, 0x87D8u, 0x8742u, 0x86ADu,
    0x8618u, 0x8583u, 0x84F0u, 0x845Cu, 0x83C9u, 0x8336u, 0x82A4u, 0x8212u,
    0x8181u, 0x80F0u, 0x8060u, 0x7FD0u, 0x7F40u, 0x7EB1u, 0x7E22u, 0x7D93u,
    0x7D05u, 0x7C78u, 0x7BEBu, 0x7B5Eu, 0x7AD2u, 0x7A46u, 0x79BAu, 0x792Fu,
    0x78A4u, 0x781Au, 0x7790u, 0x7706u, 0x767Du, 0x75F5u, 0x756Cu, 0x74E4u,
    0x745Du, 0x73D5u, 0x734Fu, 0x72C8u, 0x7242u, 0x71BCu, 0x7137u, 0x70B2u,
    0x702Eu, 0x6FA9u, 0x6F26u, 0x6EA2u, 0x6E1Fu, 0x6D9Cu, 0x6D1Au, 0x6C98u,
    0x6C16u, 0x6B95u, 0x6B14u, 0x6A94u, 0x6A13u, 0x6993u, 0x6914u, 0x6895u,
    0x6816u, 0x6798u, 0x6719u, 0x669Cu, 0x661Eu, 0x65A1u, 0x6524u, 0x64A8u,
    0x642Cu, 0x63B0u, 0x6335u, 0x62BAu, 0x623Fu, 0x61C5u, 0x614Bu, 0x60D1u,
    0x6058u, 0x5FDFu, 0x5F66u, 0x5EEDu, 0x5E75u, 0x5DFDu, 0x5D86u, 0x5D0Fu,
    0x5C98u, 0x5C22u, 0x5BABu, 0x5B35u, 0x5AC0u, 0x5A4Bu, 0x59D6u, 0x5961u,
    0x58EDu, 0x5879u, 0x5805u, 0x5791u, 0x571Eu, 0x56ACu, 0x5639u, 0x55C7u,
    0x5555u, 0x54E3u, 0x5472u, 0x5401u, 0x5390u, 0x5320u, 0x52AFu, 0x5240u,
    0x51D0u, 0x5161u, 0x50F2u, 0x5083u, 0x5015u, 0x4FA6u, 0x4F38u, 0x4ECBu,
    0x4E5Eu, 0x4DF1u, 0x4D84u, 0x4D17u, 0x4CABu, 0x4C3Fu, 0x4BD3u, 0x4B68u,
    0x4AFDu, 0x4A92u, 0x4A27u, 0x49BDu, 0x4953u, 0x48E9u, 0x4880u, 0x4817u,
    0x47AEu, 0x4745u, 0x46DCu, 0x4674u, 0x460Cu, 0x45A5u, 0x453Du, 0x44D6u,
    0x446Fu, 0x4408u, 0x43A2u, 0x433Cu, 0x42D6u, 0x4270u, 0x420Bu, 0x41A6u,
    0x4141u, 0x40DCu, 0x4078u, 0x4014u, 0x3FB0u, 0x3F4Cu, 0x3EE8u, 0x3E85u,
    0x3E22u, 0x3DC0u, 0x3D5Du, 0x3CFBu, 0x3C99u, 0x3C37u, 0x3BD6u, 0x3B74u,
    0x3B13u, 0x3AB2u, 0x3A52u, 0x39F1u, 0x3991u, 0x3931u, 0x38D2u, 0x3872u,
    0x3813u, 0x37B4u, 0x3755u, 0x36F7u, 0x3698u, 0x363Au, 0x35DCu, 0x357Fu,
    0x3521u, 0x34C4u, 0x3467u, 0x340Au, 0x33AEu, 0x3351u, 0x32F5u, 0x3299u,
    0x323Eu, 0x31E2u, 0x3187u, 0x312Cu, 0x30D1u, 0x3076u, 0x301Cu, 0x2FC2u,
    0x2F68u, 0x2F0Eu, 0x2EB4u, 0x2E5Bu, 0x2E02u, 0x2DA9u, 0x2D50u, 0x2CF8u,
    0x2C9Fu, 0x2C47u, 0x2BEFu, 0x2B97u, 0x2B40u, 0x2AE8u, 0x2A91u, 0x2A3Au,
    0x29E4u, 0x298Du, 0x2937u, 0x28E0u, 0x288Bu, 0x2835u, 0x27DFu, 0x278Au,
    0x2735u, 0x26E0u, 0x268Bu, 0x2636u, 0x25E2u, 0x258Du, 0x2539u, 0x24E5u,
    0x2492u, 0x243Eu, 0x23EBu, 0x2398u, 0x2345u, 0x22F2u, 0x22A0u, 0x224Du,
    0x21FBu, 0x21A9u, 0x2157u, 0x2105u, 0x20B4u, 0x2063u, 0x2012u, 0x1FC1u,
    0x1F70u, 0x1F1Fu, 0x1ECFu, 0x1E7Fu, 0x1E2Eu, 0x1DDFu, 0x1D8Fu, 0x1D3Fu,
    0x1CF0u, 0x1CA1u, 0x1C52u, 0x1C03u, 0x1BB4u, 0x1B66u, 0x1B17u, 0x1AC9u,
    0x1A7Bu, 0x1A2Du, 0x19E0u, 0x1992u, 0x1945u, 0x18F8u, 0x18ABu, 0x185Eu,
    0x1811u, 0x17C4u, 0x1778u, 0x172Cu, 0x16E0u, 0x1694u, 0x1648u, 0x15FDu,
    0x15B1u, 0x1566u, 0x151Bu, 0x14D0u, 0x1485u, 0x143Bu, 0x13F0u, 0x13A6u,
    0x135Cu, 0x1312u, 0x12C8u, 0x127Fu, 0x1235u, 0x11ECu, 0x11A3u, 0x1159u,
    0x1111u, 0x10C8u, 0x107Fu, 0x1037u, 0x0FEFu, 0x0FA6u, 0x0F5Eu, 0x0F17u,
    0x0ECFu, 0x0E87u, 0x0E40u, 0x0DF9u, 0x0DB2u, 0x0D6Bu, 0x0D24u, 0x0CDDu,
    0x0C97u, 0x0C50u, 0x0C0Au, 0x0BC4u, 0x0B7Eu, 0x0B38u, 0x0AF2u, 0x0AADu,
    0x0A68u, 0x0A22u, 0x09DDu, 0x0998u, 0x0953u, 0x090Fu, 0x08CAu, 0x0886u,
    0x0842u, 0x07FDu, 0x07B9u, 0x0776u, 0x0732u, 0x06EEu, 0x06ABu, 0x0668u,
    0x0624u, 0x05E1u, 0x059Eu, 0x055Cu, 0x0519u, 0x04D6u, 0x0494u, 0x0452u,
    0x0410u, 0x03CEu, 0x038Cu, 0x034Au, 0x0309u, 0x02C7u, 0x0286u, 0x0245u,
    0x0204u, 0x01C3u, 0x0182u, 0x0141u, 0x0101u, 0x00C0u, 0x0080u, 0x0040u,
    0x6A09u, 0xFFFFu, 0x6955u, 0xFF00u, 0x68A1u, 0xFE02u, 0x67EFu, 0xFD06u,
    0x673Eu, 0xFC0Bu, 0x668Du, 0xFB12u, 0x65DEu, 0xFA1Au, 0x6530u, 0xF923u,
    0x6482u, 0xF82Eu, 0x63D6u, 0xF73Bu, 0x632Bu, 0xF648u, 0x6280u, 0xF557u,
    0x61D7u, 0xF467u, 0x612Eu, 0xF379u, 0x6087u, 0xF28Cu, 0x5FE0u, 0xF1A0u,
    0x5F3Au, 0xF0B6u, 0x5E95u, 0xEFCDu, 0x5DF1u, 0xEEE5u, 0x5D4Eu, 0xEDFFu,
    0x5CACu, 0xED19u, 0x5C0Bu, 0xEC35u, 0x5B6Bu, 0xEB52u, 0x5ACBu, 0xEA71u,
    0x5A2Cu, 0xE990u, 0x598Fu, 0xE8B1u, 0x58F2u, 0xE7D3u, 0x5855u, 0xE6F6u,
    0x57BAu, 0xE61Bu, 0x5720u, 0xE540u, 0x5686u, 0xE467u, 0x55EDu, 0xE38Eu,
    0x5555u, 0xE2B7u, 0x54BEu, 0xE1E1u, 0x5427u, 0xE10Du, 0x5391u, 0xE039u,
    0x52FCu, 0xDF66u, 0x5268u, 0xDE94u, 0x51D5u, 0xDDC4u, 0x5142u, 0xDCF4u,
    0x50B0u, 0xDC26u, 0x501Fu, 0xDB59u, 0x4F8Eu, 0xDA8Cu, 0x4EFEu, 0xD9C1u,
    0x4E6Fu, 0xD8F7u, 0x4DE1u, 0xD82Du, 0x4D53u, 0xD765u, 0x4CC6u, 0xD69Eu,
    0x4C3Au, 0xD5D7u, 0x4BAFu, 0xD512u, 0x4B24u, 0xD44Eu, 0x4A9Au, 0xD38Au,
    0x4A10u, 0xD2C8u, 0x4987u, 0xD206u, 0x48FFu, 0xD146u, 0x4878u, 0xD086u,
    0x47F1u, 0xCFC7u, 0x476Bu, 0xCF0Au, 0x46E5u, 0xCE4Du, 0x4660u, 0xCD91u,
    0x45DCu, 0xCCD6u, 0x4558u, 0xCC1Bu, 0x44D5u, 0xCB62u, 0x4453u, 0xCAA9u,
    0x43D1u, 0xC9F2u, 0x434Fu, 0xC93Bu, 0x42CFu, 0xC885u, 0x424Fu, 0xC7D0u,
    0x41CFu, 0xC71Cu, 0x4151u, 0xC669u, 0x40D2u, 0xC5B6u, 0x4055u, 0xC504u,
    0x3FD8u, 0xC453u, 0x3F5Bu, 0xC3A3u, 0x3EDFu, 0xC2F4u, 0x3E64u, 0xC245u,
    0x3DE9u, 0xC198u, 0x3D6Eu, 0xC0EBu, 0x3CF5u, 0xC03Fu, 0x3C7Cu, 0xBF93u,
    0x3C03u, 0xBEE9u, 0x3B8Bu, 0xBE3Fu, 0x3B13u, 0xBD96u, 0x3A9Cu, 0xBCEDu,
    0x3A26u, 0xBC46u, 0x39B0u, 0xBB9Fu, 0x393Au, 0xBAF8u, 0x38C5u, 0xBA53u,
    0x3851u, 0xB9AEu, 0x37DDu, 0xB90Au, 0x3769u, 0xB867u, 0x36F6u, 0xB7C5u,
    0x3684u, 0xB723u, 0x3612u, 0xB681u, 0x35A0u, 0xB5E1u, 0x352Fu, 0xB541u,
    0x34BFu, 0xB4A2u, 0x344Fu, 0xB404u, 0x33DFu, 0xB366u, 0x3370u, 0xB2C9u,
    0x3302u, 0xB22Cu, 0x3293u, 0xB191u, 0x3226u, 0xB0F5u, 0x31B9u, 0xB05Bu,
    0x314Cu, 0xAFC1u, 0x30DFu, 0xAF28u, 0x3074u, 0xAE8Fu, 0x3008u, 0xADF7u,
    0x2F9Du, 0xAD60u, 0x2F33u, 0xACC9u, 0x2EC8u, 0xAC33u, 0x2E5Fu, 0xAB9Eu,
    0x2DF6u, 0xAB09u, 0x2D8Du, 0xAA75u, 0x2D24u, 0xA9E1u, 0x2CBCu, 0xA94Eu,
    0x2C55u, 0xA8BCu, 0x2BEEu, 0xA82Au, 0x2B87u, 0xA799u, 0x2B21u, 0xA708u,
    0x2ABBu, 0xA678u, 0x2A55u, 0xA5E8u, 0x29F0u, 0xA559u, 0x298Bu, 0xA4CBu,
    0x2927u, 0xA43Du, 0x28C3u, 0xA3B0u, 0x2860u, 0xA323u, 0x27FDu, 0xA297u,
    0x279Au, 0xA20Bu, 0x2738u, 0xA180u, 0x26D6u, 0xA0F6u, 0x2674u, 0xA06Cu,
    0x2613u, 0x9FE2u, 0x25B2u, 0x9F59u, 0x2552u, 0x9ED1u, 0x24F2u, 0x9E49u,
    0x2492u, 0x9DC2u, 0x2432u, 0x9D3Bu, 0x23D3u, 0x9CB4u, 0x2375u, 0x9C2Fu,
    0x2317u, 0x9BA9u, 0x22B9u, 0x9B25u, 0x225Bu, 0x9AA0u, 0x21FEu, 0x9A1Cu,
    0x21A1u, 0x9999u, 0x2145u, 0x9916u, 0x20E8u, 0x9894u, 0x208Du, 0x9812u,
    0x2031u, 0x9791u, 0x1FD6u, 0x9710u, 0x1F7Bu, 0x968Fu, 0x1F21u, 0x960Fu,
    0x1EC7u, 0x9590u, 0x1E6Du, 0x9511u, 0x1E13u, 0x9492u, 0x1DBAu, 0x9414u,
    0x1D61u, 0x9397u, 0x1D09u, 0x931Au, 0x1CB1u, 0x929Du, 0x1C59u, 0x9221u,
    0x1C01u, 0x91A5u, 0x1BAAu, 0x9129u, 0x1B53u, 0x90AFu, 0x1AFCu, 0x9034u,
    0x1AA6u, 0x8FBAu, 0x1A50u, 0x8F40u, 0x19FAu, 0x8EC7u, 0x19A5u, 0x8E4Fu,
    0x1950u, 0x8DD6u, 0x18FBu, 0x8D5Eu, 0x18A7u, 0x8CE7u, 0x1853u, 0x8C70u,
    0x17FFu, 0x8BF9u, 0x17ABu, 0x8B83u, 0x1758u, 0x8B0Du, 0x1705u, 0x8A98u,
    0x16B2u, 0x8A23u, 0x1660u, 0x89AEu, 0x160Du, 0x893Au, 0x15BCu, 0x88C6u,
    0x156Au, 0x8853u, 0x1519u, 0x87E0u, 0x14C8u, 0x876Du, 0x1477u, 0x86FBu,
    0x1426u, 0x8689u, 0x13D6u, 0x8618u, 0x1386u, 0x85A7u, 0x1337u, 0x8536u,
    0x12E7u, 0x84C6u, 0x1298u, 0x8456u, 0x1249u, 0x83E7u, 0x11FBu, 0x8377u,
    0x11ACu, 0x8309u, 0x115Eu, 0x829Au, 0x1111u, 0x822Cu, 0x10C3u, 0x81BFu,
    0x1076u, 0x8151u, 0x1029u, 0x80E4u, 0x0FDCu, 0x8078u, 0x0F8Fu, 0x800Cu,
    0x0F43u, 0x7FA0u, 0x0EF7u, 0x7F34u, 0x0EABu, 0x7EC9u, 0x0E60u, 0x7E5Eu,
    0x0E15u, 0x7DF4u, 0x0DCAu, 0x7D8Au, 0x0D7Fu, 0x7D20u, 0x0D34u, 0x7CB6u,
    0x0CEAu, 0x7C4Du, 0x0CA0u, 0x7BE5u, 0x0C56u, 0x7B7Cu, 0x0C0Cu, 0x7B14u,
    0x0BC3u, 0x7AACu, 0x0B7Au, 0x7A45u, 0x0B31u, 0x79DEu, 0x0AE8u, 0x7977u,
    0x0AA0u, 0x7911u, 0x0A58u, 0x78ABu, 0x0A10u, 0x7845u, 0x09C8u, 0x77DFu,
    0x0981u, 0x777Au, 0x0939u, 0x7715u, 0x08F2u, 0x76B1u, 0x08ABu, 0x764Du,
    0x0865u, 0x75E9u, 0x081Eu, 0x7585u, 0x07D8u, 0x7522u, 0x0792u, 0x74BFu,
    0x074Du, 0x745Du, 0x0707u, 0x73FAu, 0x06C2u, 0x7398u, 0x067Du, 0x7337u,
    0x0638u, 0x72D5u, 0x05F3u, 0x7274u, 0x05AFu, 0x7213u, 0x056Au, 0x71B3u,
    0x0526u, 0x7152u, 0x04E2u, 0x70F2u, 0x049Fu, 0x7093u, 0x045Bu, 0x7033u,
    0x0418u, 0x6FD4u, 0x03D5u, 0x6F76u, 0x0392u, 0x6F17u, 0x0350u, 0x6EB9u,
    0x030Du, 0x6E5Bu, 0x02CBu, 0x6DFDu, 0x0289u, 0x6DA0u, 0x0247u, 0x6D43u,
    0x0206u, 0x6CE6u, 0x01C4u, 0x6C8Au, 0x0183u, 0x6C2Du, 0x0142u, 0x6BD1u,
    0x0101u, 0x6B76u, 0x00C0u, 0x6B1Au, 0x0080u, 0x6ABFu, 0x0040u, 0x6A64u,
};

/* ---- VRCP family ----------------------------------------------------- */

/* Core of the RSP divide unit: the input is the sign-extended DivIn (the
 * full 32-bit value for the double-precision vrcph/vrcpl pair, or the
 * sign-extended 16-bit element for single-precision vrcp). Returns the
 * 32-bit DivOut. */
static int32_t rsp_div_core(int32_t in32)
{
    int32_t data = in32;
    int32_t addr;
    int shift;
    int32_t out;

    if (data < 0)
        data = (data >= -32768) ? -data : ~data;

    addr = data;
    shift = 0;
    if (data != 0)
    {
        for (shift = 0; addr >= 0; addr <<= 1, shift++)
            ;
    }
    addr = (addr >> 22) & 0x1ff;
    shift ^= 31;
    out = (int32_t)((0x40000000u | ((uint32_t)div_rom[addr] << 14)) >> shift);
    if (in32 == 0)
        out = 0x7fffffff;
    else if (in32 == -32768)
        out = (int32_t)0xffff0000;
    else if (in32 < 0)
        out = ~out;
    return out;
}

int32_t rsp_rcp32(int32_t in32)
{
    return rsp_div_core(in32);
}

int32_t rsp_rcp16(int32_t in16)
{
    return rsp_div_core((int32_t)(int16_t)in16);
}

/* The vrsq (reciprocal square root) table path: the second half of the
 * divide ROM, addressed with the normalization shift's parity in bit 0,
 * and the result shift halved. */
int32_t rsp_rsq32(int32_t in32)
{
    int32_t data = in32;
    int32_t addr;
    int shift;
    int32_t out;

    if (data < 0)
        data = (data >= -32768) ? -data : ~data;

    addr = data;
    shift = 0;
    if (data != 0)
    {
        for (shift = 0; addr >= 0; addr <<= 1, shift++)
            ;
    }
    addr = (addr >> 22) & 0x1ff;
    addr &= 0x1fe;
    addr |= 0x200 | (shift & 1);
    shift ^= 31;
    shift >>= 1;
    out = (int32_t)((0x40000000u | ((uint32_t)div_rom[addr] << 14)) >> shift);
    if (in32 == 0)
        out = 0x7fffffff;
    else if (in32 == -32768)
        out = (int32_t)0xffff0000;
    else if (in32 < 0)
        out = ~out;
    return out;
}

/* ---- RSP vector-unit scalar models ----------------------------------- */

/* Per-lane 48-bit accumulator, kept sign-extended in 64 bits. */
typedef int64_t RspAcc;

#define U16(x) ((int32_t)((x) & 0xffffu))
#define S16(x) ((int32_t)(int16_t)(x))

static int32_t clamp_s16(int32_t v)
{
    if (v < -32768) return -32768;
    if (v >  32767) return  32767;
    return v;
}

/* Signed clamp of the accumulator's 47..16 region, as VMADM/VMADH return. */
static int32_t acc_clamp_mid(RspAcc a)
{
    int64_t hi = a >> 16;
    if (hi < -32768) return -32768;
    if (hi >  32767) return  32767;
    return (int32_t)(hi & 0xffff);
}

/* The VMADL/VMADN "clamp low" rule: low 16 bits when 47..16 fits s16,
 * else 0x0000 / 0xffff for under-/overflow. */
static int32_t acc_clamp_low(RspAcc a)
{
    int64_t hi = a >> 16;
    if (hi < -32768) return 0x0000;
    if (hi >  32767) return 0xffff;
    return (int32_t)(a & 0xffff);
}

/* Product domains. Inputs are 16-bit lane values (any int holding them).
 * The unsigned*unsigned product can reach 0xFFFE0001, which overflows a
 * signed 32-bit multiply, so every product is carried out at 64-bit width. */
static RspAcc p_udl(int32_t a, int32_t b) { return (RspAcc)(((int64_t)U16(a) * (int64_t)U16(b)) >> 16); }
static RspAcc p_udm(int32_t a, int32_t b) { return (RspAcc)((int64_t)S16(a) * (int64_t)U16(b)); }
static RspAcc p_udn(int32_t a, int32_t b) { return (RspAcc)((int64_t)U16(a) * (int64_t)S16(b)); }
static RspAcc p_udh(int32_t a, int32_t b) { return ((RspAcc)((int64_t)S16(a) * (int64_t)S16(b))) << 16; }

/* 32-bit (int:frac lane pair) helpers. */
typedef struct { int32_t i, f; } Rsp32;

static int32_t r32(Rsp32 v) { /* sign-extended combined value, for analysis */
    return (int32_t)(((uint32_t)S16(v.i) << 16) | (uint32_t)U16(v.f));
}
static Rsp32 mk32(int32_t w)
{
    Rsp32 v;
    v.i = (w >> 16) & 0xffff;
    v.f = w & 0xffff;
    return v;
}

/* Canonical 32x32 fixed multiply chain:
 *   vmudl af*bf; vmadm ai*bf; vmadn out_f af*bi; vmadh out_i ai*bi
 * Returns the (i,f) pair with the exact extraction clamps, and optionally
 * exposes the accumulator for continued vmad chains. */
static Rsp32 mac32(Rsp32 a, Rsp32 b, RspAcc *acc_out)
{
    Rsp32 o;
    RspAcc acc = p_udl(a.f, b.f);
    acc += p_udm(a.i, b.f);
    acc += p_udn(a.f, b.i);
    o.f = acc_clamp_low(acc);
    acc += p_udh(a.i, b.i);
    o.i = acc_clamp_mid(acc);
    if (acc_out) *acc_out = acc;
    return o;
}

/* ---- vertex 1/w ------------------------------------------------------- */

/* The vertex-processing reciprocal: raw vrcp32, then the microcode's
 * Newton-Raphson step r' = r * (4 - 4 * w * r) carried out with the exact
 * multiply chains ($v31[1] = 4, $v31[4] = -4). Input is the clip-space w
 * in s15.16; output the stored VTX_INV_W 32-bit value. */
int32_t rsp_vtx_invw(int32_t w)
{
    Rsp32 wv = mk32(w);
    Rsp32 r  = mk32(rsp_rcp32(w));
    Rsp32 t, u, o;
    RspAcc acc;

    /* t = w * r */
    t = mac32(wv, r, 0);

    /* u = 4 - 4*t :  vmudh vOne*4 ; vmadn t_f*-4 ; vmadh t_i*-4 */
    acc = p_udh(1, 4);
    acc += p_udn(t.f, -4);
    u.f = acc_clamp_low(acc);
    acc += p_udh(t.i, -4);
    u.i = acc_clamp_mid(acc);

    /* r' = u * r */
    o = mac32(u, r, 0);
    return (int32_t)(((uint32_t)U16(o.i) << 16) | (uint32_t)U16(o.f));
}

/* ---- lighting ---------------------------------------------------------- */

/* VMULF/VMACF accumulator helpers: product terms are doubled, VMULF seeds
 * the +0x8000 rounding bias, and the result reads are the signed (vmacf) or
 * unsigned (vmacu) clamps of accumulator bits 47..16. */
static RspAcc acc48(RspAcc a)
{
    a &= (((RspAcc)1 << 48) - 1);
    if (a & ((RspAcc)1 << 47))
        a -= ((RspAcc)1 << 48);
    return a;
}

static int32_t acc_read_signed(RspAcc a)
{
    int64_t hi = acc48(a) >> 16;
    if (hi < -32768) return 0x8000;
    if (hi >  32767) return 0x7fff;
    return (int32_t)(hi & 0xffff);
}

static int32_t acc_read_unsigned(RspAcc a)
{
    int64_t sa = acc48(a);
    int64_t hi = sa >> 16;
    if (sa < 0)      return 0x0000;
    if (hi > 32767)  return 0xffff;
    return (int32_t)(hi & 0xffff);
}

/* The continue_light_dir_xfrm pass: one s8 direction is rotated from camera
 * to model space by the modelview 3x3 transpose with the exact MAC chain
 * (direction bytes enter as s8 << 8 from lpv), the squared length is summed
 * with the vaddc/vadd carry adds, the vrsq reciprocal square root
 * normalizes, the result is scaled by 0x100, and the s8 the microcode
 * stores with spv is the top byte of the integer lane. mv is the s15.16
 * modelview matrix; dir/out are s8 triples. */
void rsp_light_dir_xfrm_one(const int32_t mv[4][4],
                            const int32_t dir[3], int32_t out[3])
{
    int32_t t_mid[3], t_hi[3];
    int32_t sq_lo[3], sq_mid[3];
    int32_t lo, ci, si;
    Rsp32 r;
    int L;

    for (L = 0; L < 3; L++)
    {
        RspAcc acc = 0;
        int ax;
        for (ax = 0; ax < 3; ax++)
        {
            int32_t d = (int32_t)(int16_t)((dir[ax] & 0xff) << 8);
            int32_t mi = (mv[L][ax] >> 16) & 0xffff;
            int32_t mf = mv[L][ax] & 0xffff;
            acc += p_udn(mf, d);
            acc += p_udh(mi, d);
        }
        acc = acc48(acc);
        t_mid[L] = (int32_t)((acc >> 16) & 0xffff);
        t_hi[L]  = (int32_t)((acc >> 32) & 0xffff);
    }
    for (L = 0; L < 3; L++)
    {
        RspAcc acc = p_udl(t_mid[L], t_mid[L]);
        acc += p_udm(t_hi[L], t_mid[L]);
        acc += p_udn(t_mid[L], t_hi[L]);
        sq_lo[L] = acc_clamp_low(acc);
        acc += p_udh(t_hi[L], t_hi[L]);
        sq_mid[L] = acc_clamp_mid(acc);
    }
    /* vaddc / vadd pair adds: X^2 + Y^2, then + Z^2 */
    {
        int32_t sum = U16(sq_lo[0]) + U16(sq_lo[1]);
        int32_t carry = (sum >> 16) & 1;
        lo = sum & 0xffff;
        si = clamp_s16(S16(sq_mid[0]) + S16(sq_mid[1]) + carry);
        sum = U16(lo) + U16(sq_lo[2]);
        carry = (sum >> 16) & 1;
        lo = sum & 0xffff;
        si = clamp_s16(si + S16(sq_mid[2]) + carry);
    }
    r = mk32(rsp_rsq32((int32_t)(((uint32_t)U16(si) << 16) | (uint32_t)U16(lo))));
    for (L = 0; L < 3; L++)
    {
        int32_t n_lo, n_mid;
        RspAcc acc = p_udl(t_mid[L], r.f);
        acc += p_udm(t_hi[L], r.f);
        acc += p_udn(t_mid[L], r.i);
        n_lo = acc_clamp_low(acc);
        acc += p_udh(t_hi[L], r.i);
        n_mid = acc_clamp_mid(acc);
        /* x 0x100 (vmudn / vmadh), then spv stores the int lane's top byte */
        acc = p_udn(n_lo, 0x100);
        acc += p_udh(n_mid, 0x100);
        n_mid = acc_clamp_mid(acc);
        out[L] = (int32_t)(signed char)((n_mid >> 8) & 0xff);
    }
}

/* One directional-light dot product: raw s8 vertex normal against the
 * transformed s8 light direction, both entering the lanes as byte << 8
 * (lpv), through the doubled vmulu/vmacu accumulator with the unsigned
 * read clamp and the & 0x7FFF mask. */
static int32_t rsp_light_dot(const int32_t n[3], const int32_t d[3])
{
    RspAcc acc = 0x8000;
    int ax;
    for (ax = 0; ax < 3; ax++)
    {
        int32_t nl = (int32_t)(int16_t)((n[ax] & 0xff) << 8);
        int32_t dl = (int32_t)(int16_t)((d[ax] & 0xff) << 8);
        acc += 2 * (RspAcc)nl * dl;
    }
    return acc_read_unsigned(acc) & 0x7fff;
}

/* The lights_dircoloraccum2 loop: colors live in the lanes as byte << 7
 * (luv), and every accumulator round folds the running color back in with
 * vmulf(color, 0x7FFF) -- two lights per round walking down from the top,
 * with a single-light tail for an odd count. The stored byte is the suv
 * lane >> 7. dirs are the transformed s8 triples; rgb byte triples; amb
 * the ambient byte triple. */
void rsp_light_vtx(const int32_t n[3], const int32_t amb[3],
                   const int32_t (*rgb)[3], const int32_t (*dirs)[3],
                   int num, int32_t out[3])
{
    int32_t lt[3];
    int c, i;
    for (c = 0; c < 3; c++)
        lt[c] = (amb[c] & 0xff) << 7;
    i = num - 1;
    while (i >= 1)
    {
        int32_t d1 = rsp_light_dot(n, dirs[i]);
        int32_t d2 = rsp_light_dot(n, dirs[i - 1]);
        for (c = 0; c < 3; c++)
        {
            RspAcc acc = 2 * (RspAcc)S16(lt[c]) * 0x7fff + 0x8000;
            acc += 2 * (RspAcc)((rgb[i][c] & 0xff) << 7) * d1;
            acc += 2 * (RspAcc)((rgb[i - 1][c] & 0xff) << 7) * d2;
            lt[c] = acc_read_signed(acc);
        }
        i -= 2;
    }
    if (i == 0)
    {
        int32_t d1 = rsp_light_dot(n, dirs[0]);
        for (c = 0; c < 3; c++)
        {
            RspAcc acc = 2 * (RspAcc)S16(lt[c]) * 0x7fff + 0x8000;
            acc += 2 * (RspAcc)((rgb[0][c] & 0xff) << 7) * d1;
            lt[c] = acc_read_signed(acc);
        }
    }
    for (c = 0; c < 3; c++)
        out[c] = (lt[c] >> 7) & 0xff;
}

/* The lights_texgenmain chain (G_TEXTURE_GEN): texture coordinates from
 * the raw s8 vertex normal dotted with the two transformed s8 lookat
 * directions, all entering the lanes as byte << 8 (lpv), through the
 * signed vmulf/vmacf accumulator. The plain coordinate is the
 * vmudh(1, 0x4000) + vmacf(0x4000, dot) chain; G_TEXTURE_GEN_LINEAR
 * continues the same accumulator with vmadh(1, 0xC000) to recenter, then
 * applies the linearGenerateCoefficients polynomial {0x44D3, 0x6CB3} on
 * the clamped lane values (the 2.08 behaviour, without the 2.04H
 * BUG_TEXGEN_LINEAR_CLOBBER_S_T accumulator clobber). */
static int32_t rsp_texgen_dot(const int32_t n[3], const int32_t d[3])
{
    RspAcc acc = 0x8000;    /* vmulf rounding bias */
    int ax;
    for (ax = 0; ax < 3; ax++)
    {
        int32_t nl = (int32_t)(int16_t)(uint16_t)((n[ax] & 0xff) << 8);
        int32_t dl = (int32_t)(int16_t)(uint16_t)((d[ax] & 0xff) << 8);
        acc += 2 * (RspAcc)nl * dl;
    }
    return (int32_t)(int16_t)acc_read_signed(acc);
}

void rsp_texgen(const int32_t n[3], const int32_t l0[3], const int32_t l1[3],
                int linear, int32_t *s_out, int32_t *t_out)
{
    int32_t dot[2];
    int32_t st[2];
    RspAcc acc[2];
    int k;
    dot[0] = rsp_texgen_dot(n, l0);     /* S lanes (lookat 0) */
    dot[1] = rsp_texgen_dot(n, l1);     /* T lanes (lookat 1) */
    for (k = 0; k < 2; k++)
    {
        /* vmudh vOne, 0x4000 then vmacf mask, dot */
        acc[k] = ((RspAcc)0x4000) << 16;
        acc[k] += 2 * (RspAcc)0x4000 * dot[k];
        st[k] = (int32_t)(int16_t)acc_read_signed(acc[k]);
    }
    if (linear)
    {
        for (k = 0; k < 2; k++)
        {
            int32_t st1, sq, v3;
            RspAcc a2;
            /* vmadh vOne, 0xC000: continue the accumulator with -0x4000 */
            acc[k] += ((RspAcc)(int32_t)(int16_t)0xC000) << 16;
            st1 = (int32_t)(int16_t)acc_read_signed(acc[k]);
            /* vmulf(st, st) */
            sq = (int32_t)(int16_t)acc_read_signed(
                (RspAcc)0x8000 + 2 * (RspAcc)st1 * st1);
            /* vmulf(st, 0x7FFF) + vmacf(st, 0x6CB3) */
            v3 = (int32_t)(int16_t)acc_read_signed(
                (RspAcc)0x8000 + 2 * (RspAcc)st1 * 0x7fff
                               + 2 * (RspAcc)st1 * 0x6cb3);
            /* vmudh vOne, 0x4000 (accumulator reset, result discarded),
             * vmacf(st, 0x44D3), vmacf(sq, v3) */
            a2 = ((RspAcc)0x4000) << 16;
            a2 += 2 * (RspAcc)st1 * 0x44d3;
            a2 += 2 * (RspAcc)sq * v3;
            st[k] = (int32_t)(int16_t)acc_read_signed(a2);
        }
    }
    *s_out = st[0];
    *t_out = st[1];
}

/* ---- clipping ---------------------------------------------------------- */

/* The clip-ratio-scaled W the vertex pipeline compares against for the
 * scaled outcodes: ratio (2) times the s15.16 w through the vmudn/vmadh
 * pair, whose mid read clamps. */
int32_t rsp_clip_scale_w(int32_t w, int ratio)
{
    RspAcc acc = p_udn(w & 0xffff, ratio) + p_udh((w >> 16) & 0xffff, ratio);
    int32_t f = acc_clamp_low(acc);
    int32_t i = acc_clamp_mid(acc);
    return (int32_t)(((uint32_t)U16(i) << 16) | (uint32_t)U16(f));
}

/* The F3DEX2 2.04H clip subdivision: compute the fade factor between the
 * on-screen and off-screen vertex against one clip condition's plane
 * (the clipRatio row), and lerp position and attributes, mirroring the
 * microcode's vector chain op for op, including the
 * BUG_CLIPPING_FAIL_WHEN_SUM_ZERO variant of the reciprocal sign fixup.
 *
 * on_pos/off_pos are the s15.16 clip-space x,y,z,w. cr is the four-short
 * clipRatio row. attr lanes are the stored vertex attribute shorts: colors
 * as byte << 7 (luv domain) in lanes 0..3 and the s10.5 texture coords in
 * lanes 4..5. Outputs: lerped position (s15.16) and the eight lerped
 * attribute lanes (acc mid clamps, exactly vPairST). */
void rsp_clip_lerp(const int32_t on_pos[4], const int32_t off_pos[4],
                   const int16_t cr[4],
                   const int16_t on_attr[8], const int16_t off_attr[8],
                   int32_t out_pos[4], int16_t out_attr[8])
{
    int32_t v8[4], v9[4], v10[4], v11[4]; /* frac/int lanes */
    int32_t co[4];
    int32_t rcp_lo, rcp_hi, abs2, fade, onfade;
    int32_t r2_lo, r2_hi, x_lo, x_hi;
    RspAcc acc;
    int L;

    /* on * cr and (on - off) * cr per lane, i:f */
    for (L = 0; L < 4; L++)
    {
        int32_t onf = on_pos[L] & 0xffff, oni = (on_pos[L] >> 16) & 0xffff;
        int32_t off = off_pos[L] & 0xffff, ofi = (off_pos[L] >> 16) & 0xffff;
        int32_t crv = S16(cr[L]);
        int32_t ncr = clamp_s16(-crv);          /* vmudh by -1 */
        acc = p_udn(onf, crv) + p_udh(oni, crv);
        v8[L] = (int32_t)(acc & 0xffff);
        v9[L] = (int32_t)((acc >> 16) & 0xffff);
        acc += p_udn(off, ncr) + p_udh(ofi, ncr);
        v10[L] = (int32_t)(acc & 0xffff);
        v11[L] = (int32_t)((acc >> 16) & 0xffff);
    }
    /* vaddc/vadd lane sums: [0q] then [1h]; only lanes 1 and 3 are
     * consumed downstream but the carries are per lane. */
    {
        int32_t s;
        s = U16(v8[1]) + U16(v8[0]);  co[1] = (s >> 16) & 1; v8[1] = s & 0xffff;
        s = U16(v8[3]) + U16(v8[2]);  co[3] = (s >> 16) & 1; v8[3] = s & 0xffff;
        v9[1] = clamp_s16(S16(v9[1]) + S16(v9[0]) + co[1]);
        v9[3] = clamp_s16(S16(v9[3]) + S16(v9[2]) + co[3]);
        s = U16(v10[1]) + U16(v10[0]); co[1] = (s >> 16) & 1; v10[1] = s & 0xffff;
        s = U16(v10[3]) + U16(v10[2]); co[3] = (s >> 16) & 1; v10[3] = s & 0xffff;
        v11[1] = clamp_s16(S16(v11[1]) + S16(v11[0]) + co[1]);
        v11[3] = clamp_s16(S16(v11[3]) + S16(v11[2]) + co[3]);
        s = U16(v8[3]) + U16(v8[1]);  co[3] = (s >> 16) & 1; v8[3] = s & 0xffff;
        v9[3] = clamp_s16(S16(v9[3]) + S16(v9[1]) + co[3]);
        s = U16(v10[3]) + U16(v10[1]); co[3] = (s >> 16) & 1; v10[3] = s & 0xffff;
        v11[3] = clamp_s16(S16(v11[3]) + S16(v11[1]) + co[3]);
    }
    /* Double-precision reciprocal of the difference sum (the leading
     * vrcph of the 2.04H build only primes DivIn; its result is
     * discarded). */
    {
        int32_t r = rsp_rcp32((int32_t)(((uint32_t)U16(v11[3]) << 16)
                                       | (uint32_t)U16(v10[3])));
        rcp_lo = r & 0xffff;
        rcp_hi = (r >> 16) & 0xffff;
    }
    /* vabs $v29, (v11 | 1), 2: +/- 2 by the sign of the int sum. The
     * vor with 1 (F3DEX2 2.05+/F3DZEX2) removes the sum-zero case the
     * 2.04H build mishandled; a zero int sum now takes the positive
     * side. */
    if (S16(v11[3] | 1) > 0)  abs2 = 2;
    else                      abs2 = -2;
    acc = p_udn(rcp_lo, abs2);
    rcp_lo = acc_clamp_low(acc);
    acc += p_udh(rcp_hi, abs2);
    rcp_hi = acc_clamp_mid(acc);
    /* veq/vmrg: keep the low half if the scaled high half is 0, else
     * saturate to 0xFFFF. */
    if (S16(rcp_hi) != 0)
        rcp_lo = 0xffff;
    /* (v11:v10) = diff * rcp ~= 1 */
    acc = p_udl(v10[3], rcp_lo) + p_udm(v11[3], rcp_lo);
    v11[3] = acc_clamp_mid(acc);
    v10[3] = acc_clamp_low(acc);
    /* second reciprocal, of the ~1 value */
    {
        int32_t r = rsp_rcp32((int32_t)(((uint32_t)U16(v11[3]) << 16)
                                       | (uint32_t)U16(v10[3])));
        r2_lo = r & 0xffff;
        r2_hi = (r >> 16) & 0xffff;
    }
    /* Newton-Raphson: x = r * v; v' = 4 - 4x; r' = r * v' */
    acc = p_udl(r2_lo, v10[3]) + p_udm(r2_hi, v10[3]);
    acc += p_udn(r2_lo, v11[3]);
    x_lo = acc_clamp_low(acc);
    acc += p_udh(r2_hi, v11[3]);
    x_hi = acc_clamp_mid(acc);
    acc = ((RspAcc)4 << 16);
    acc += p_udn(x_lo, -4);
    x_lo = acc_clamp_low(acc);
    acc += p_udh(x_hi, -4);
    x_hi = acc_clamp_mid(acc);
    acc = p_udl(r2_lo, x_lo) + p_udm(r2_hi, x_lo);
    acc += p_udn(r2_lo, x_hi);
    r2_lo = acc_clamp_low(acc);
    acc += p_udh(r2_hi, x_hi);
    r2_hi = acc_clamp_mid(acc);
    /* A * refined reciprocal, then * rcp again */
    acc = p_udl(v8[3], r2_lo) + p_udm(v9[3], r2_lo);
    acc += p_udn(v8[3], r2_hi);
    v10[3] = acc_clamp_low(acc);
    acc += p_udh(v9[3], r2_hi);
    v11[3] = acc_clamp_mid(acc);
    acc = p_udl(v10[3], rcp_lo) + p_udm(v11[3], rcp_lo);
    v11[3] = acc_clamp_mid(acc);
    v10[3] = acc_clamp_low(acc);
    /* Clamp the factor to (0, 1]: vlt/vmrg/vsubc/vge/vmrg on lane 3.
     * vlt: VCC = (int < 1); vmrg picks frac or 0xFFFF.
     * vsubc frac - 1 sets VCO: co = borrow (frac == 0), ne = (frac != 1).
     * vge: VCC = (int > 0) | ((int == 0) & !(ne & co)); vmrg picks frac
     * or 1. */
    {
        int32_t fi = S16(v11[3]);
        int32_t ff = U16(v10[3]);
        int32_t vcc = (fi < 1);
        int32_t ne, cob;
        ff = vcc ? ff : 0xffff;
        fi = (fi < 1) ? fi : 1;            /* vlt result = min */
        cob = (ff - 1 < 0);
        ne  = (ff != 1);
        vcc = (fi > 0) | ((fi == 0) & !(ne & cob));
        fade = vcc ? ff : 1;
    }
    acc = p_udn(fade, -1);
    onfade = acc_clamp_low(acc);            /* 0x10000 - fade, low half */
    /* Position lerp: off * fade + on * onfade through the i:f MAC chain */
    for (L = 0; L < 4; L++)
    {
        int32_t onf = on_pos[L] & 0xffff, oni = (on_pos[L] >> 16) & 0xffff;
        int32_t off = off_pos[L] & 0xffff, ofi = (off_pos[L] >> 16) & 0xffff;
        int32_t pi, pf;
        acc  = p_udl(off, fade) + p_udm(ofi, fade);
        acc += p_udl(onf, onfade);
        acc += p_udm(oni, onfade);
        pi = acc_clamp_mid(acc);
        pf = acc_clamp_low(acc);
        out_pos[L] = (int32_t)(((uint32_t)U16(pi) << 16) | (uint32_t)U16(pf));
    }
    /* Attribute lerp: one vmudm/vmadm round (signed attrs, unsigned
     * factors), acc mid clamps out. */
    for (L = 0; L < 8; L++)
    {
        acc  = p_udm(S16(off_attr[L]) & 0xffff, fade);
        acc += p_udm(S16(on_attr[L]) & 0xffff, onfade);
        out_attr[L] = (int16_t)acc_clamp_mid(acc);
    }
}

/* ---- vertex screen transform ------------------------------------------ */

/* The microcode's vertex write, transcribed: the clip-space position is
 * scaled by perspNorm, the reciprocal (with the 4/-4 Newton step) is taken
 * of the *perspNorm'd* w, the raw position is multiplied by that 32-bit
 * reciprocal, scaled by perspNorm a second time, and mapped through the
 * viewport with the same MAC chains the RSP uses. Outputs the 10.2 screen
 * x/y, the 16.16 screen z, and the VTX_INV_W value the triangle write
 * reads back. Returns 0 when the input is outside the transcribed domain
 * (w <= 0) and the caller must use its own projection. */
int rsp_vtx_screen(int32_t cx, int32_t cy, int32_t cz, int32_t cw,
                   int32_t pn,
                   int32_t vsx, int32_t vsy, int32_t vsz,
                   int32_t vtx_, int32_t vty, int32_t vtz,
                   int32_t *sx102, int32_t *sy102, int32_t *sz1616,
                   int32_t *invw_out)
{
    Rsp32 pos[3];
    Rsp32 pw;
    Rsp32 r, t, u, iw;
    int32_t scr_i[3], scr_f[3];
    RspAcc acc;
    int lane;

    if (cw <= 0)
        return 0;

    pos[0] = mk32(cx);
    pos[1] = mk32(cy);
    pos[2] = mk32(cz);

    /* perspNorm'd w (vmudl/vmadm with vVpMisc[4]) */
    {
        Rsp32 w32 = mk32(cw);
        acc = p_udl(w32.f, pn);
        acc += p_udm(w32.i, pn);
        pw.i = acc_clamp_mid(acc);
        pw.f = acc_clamp_low(acc);
    }

    /* 1/w' with the Newton-Raphson step, exactly as rsp_vtx_invw */
    r = mk32(rsp_rcp32((int32_t)(((uint32_t)U16(pw.i) << 16) | (uint32_t)U16(pw.f))));
    t = mac32(pw, r, 0);
    acc = p_udh(1, 4);
    acc += p_udn(t.f, -4);
    u.f = acc_clamp_low(acc);
    acc += p_udh(t.i, -4);
    u.i = acc_clamp_mid(acc);
    iw = mac32(u, r, 0);
    if (invw_out)
        *invw_out = (int32_t)(((uint32_t)U16(iw.i) << 16) | (uint32_t)U16(iw.f));

    /* raw position x reciprocal, then x perspNorm, then viewport */
    for (lane = 0; lane < 3; lane++)
    {
        Rsp32 p1, p2;
        int32_t vs = (lane == 0) ? vsx : (lane == 1) ? -vsy : vsz;
        int32_t vt = (lane == 0) ? vtx_ : (lane == 1) ? vty : vtz;

        acc = p_udl(pos[lane].f, iw.f);
        acc += p_udm(pos[lane].i, iw.f);
        acc += p_udn(pos[lane].f, iw.i);
        p1.f = acc_clamp_low(acc);
        acc += p_udh(pos[lane].i, iw.i);
        p1.i = acc_clamp_mid(acc);

        acc = p_udl(p1.f, pn);
        acc += p_udm(p1.i, pn);
        p2.i = acc_clamp_mid(acc);
        p2.f = acc_clamp_low(acc);

        acc = p_udh(vt, 1);
        acc += p_udn(p2.f, vs);
        scr_f[lane] = acc_clamp_low(acc);
        acc += p_udh(p2.i, vs);
        scr_i[lane] = acc_clamp_mid(acc);
    }

    *sx102 = (int32_t)(int16_t)scr_i[0];
    *sy102 = (int32_t)(int16_t)scr_i[1];
    /* vertices_store clamps the screen z's integer lane to >= 0 with vge
     * before storing VTX_SCR_Z; the fraction halfword is stored from the
     * unclamped register. Geometry behind z = 0 (no-z billboards) carries
     * the clamped integer with the raw fraction into the triangle write's
     * fourth texture lane. */
    {
        int32_t zi = (int32_t)(int16_t)scr_i[2];
        if (zi < 0)
            zi = 0;
        *sz1616 = (int32_t)(((uint32_t)U16(zi) << 16) | (uint32_t)U16(scr_f[2]));
    }
    return 1;
}

/* The fog factor: the microcode routes the clip-space z through the same
 * position pipeline in the w lane (pos x invw, x perspNorm, then the
 * viewport MAC with fogMult/fogOffset in the fog lanes plus the masked
 * +0x7F00 bias), clamps the result to >= 0x7F00 with vge, and stores the
 * low byte as the vertex alpha. */
int32_t rsp_vtx_fog(int32_t cz, int32_t cw, int32_t pn,
                    int32_t fog_m, int32_t fog_o)
{
    Rsp32 pw, r, t, u, iw, zp, p1, p2;
    RspAcc acc;
    int32_t lane;

    if (cw <= 0)
        return 0;

    {
        Rsp32 w32 = mk32(cw);
        acc = p_udl(w32.f, pn);
        acc += p_udm(w32.i, pn);
        pw.i = acc_clamp_mid(acc);
        pw.f = acc_clamp_low(acc);
    }
    r = mk32(rsp_rcp32((int32_t)(((uint32_t)U16(pw.i) << 16) | (uint32_t)U16(pw.f))));
    t = mac32(pw, r, 0);
    acc = p_udh(1, 4);
    acc += p_udn(t.f, -4);
    u.f = acc_clamp_low(acc);
    acc += p_udh(t.i, -4);
    u.i = acc_clamp_mid(acc);
    iw = mac32(u, r, 0);

    zp = mk32(cz);
    acc = p_udl(zp.f, iw.f);
    acc += p_udm(zp.i, iw.f);
    acc += p_udn(zp.f, iw.i);
    p1.f = acc_clamp_low(acc);
    acc += p_udh(zp.i, iw.i);
    p1.i = acc_clamp_mid(acc);

    acc = p_udl(p1.f, pn);
    acc += p_udm(p1.i, pn);
    p2.i = acc_clamp_mid(acc);
    p2.f = acc_clamp_low(acc);

    acc = p_udh(fog_o, 1);
    acc += p_udh(1, 0x7f00);
    acc += p_udn(p2.f, fog_m);
    acc += p_udh(p2.i, fog_m);
    lane = (int32_t)(int16_t)acc_clamp_mid(acc);

    if (lane < 0x7f00)
        lane = 0x7f00;
    return lane & 0xff;
}

/* ---- triangle write ---------------------------------------------------- */

/* VCR crimp of the slope integer lanes against the microcode's v30[3]:
 * values at or above the bound clamp to it, values below its ones'
 * complement clamp to the complement. The 2.0xH-era F3DEX2 OoT ships
 * uses 0x1CC; F3DZEX2 ships 0x100. */
static int32_t vcr_bound_clamp(int32_t v, int32_t bound)
{
    int32_t s = S16(v);
    int32_t nb = ~bound;
    if (s >= bound)
        return bound;
    if (s <= nb)
        return nb;
    return s;
}

/* 32-bit double-precision subtract (vsubc on frac, vsub on int with the
 * carry). The register results have the int lane signed-clamped. */
static Rsp32 sub32(Rsp32 a, Rsp32 b)
{
    Rsp32 o;
    int borrow = (U16(a.f) < U16(b.f));
    o.f = (a.f - b.f) & 0xffff;
    o.i = clamp_s16(S16(a.i) - S16(b.i) - borrow) & 0xffff;
    return o;
}

int rsp_tri_write(int32_t *ew,
                  const RspTriVtx *v1c, const RspTriVtx *v2c,
                  const RspTriVtx *v3c,
                  int textured, int z_buffered, int smooth,
                  int tile, int level,
                  int32_t dx_scale, int32_t idy_scale,
                  int32_t frac_mask, int32_t vcr_bound)
{
    const RspTriVtx *vh, *vm, *vl, *tmpv;
    int32_t cross_i, cross_f;
    Rsp32 inv_cross, nr, inv_dx;
    int32_t inv_dy_m_32, inv_dy_l_32;     /* single-precision reciprocals */
    Rsp32 inv_dy_m_sc, inv_dy_l_sc, inv_dy_lm_sc;
    Rsp32 dxldy, dxmdy, dxhdy;
    int32_t dxldy_f2, dxmdy_f2, dxhdy_f2;
    int32_t y_spx_i, y_spx_f;
    Rsp32 xh, xm;
    int32_t max_iw;
    Rsp32 inv_max;
    Rsp32 wnorm[3];
    int32_t at_i[3][8], at_f[3][8];       /* [vertex][lane r g b a s t w z] */
    Rsp32 dA_H[8], dA_M[8], dA_x[8], dA_y[8], dAdX[8], dAdY[8], dAdE[8], base[8];
    RspAcc acc;
    Rsp32 pre_z_base, pre_z_dAdX, pre_z_dAdE, pre_z_dAdY;
    int lft, k;
    int nw;
    int32_t inv_dy_lm_32;
    int32_t mh_x, mh_y, lh_x, lh_y, hl_y, hm_x, lm_x, lm_y;

    /* ---- sort by screen y (10.2), exact vlt/vge/vmrg tie rules ---- */
    {
        const RspTriVtx *lo12, *hi12, *v4;
        int32_t min12, max12, t6;
        int vcc;
        vcc  = (S16(v1c->y) <  S16(v2c->y));
        min12 = vcc ? v1c->y : v2c->y;
        lo12  = vcc ? v1c : v2c;
        vcc  = (S16(v1c->y) >= S16(v2c->y));
        max12 = vcc ? v1c->y : v2c->y;
        hi12  = vcc ? v1c : v2c;
        vcc  = (S16(min12) >= S16(v3c->y));
        v4   = vcc ? lo12 : v3c;
        vh   = vcc ? v3c : lo12;
        t6   = vcc ? min12 : v3c->y;     /* vge result value: max(min12, y3) */
        vcc  = (S16(t6) < S16(max12));
        vm   = vcc ? v4 : hi12;
        vl   = vcc ? hi12 : v4;
        tmpv = 0; (void)tmpv;
    }

    mh_x = S16(vm->x) - S16(vh->x);  mh_y = S16(vm->y) - S16(vh->y);
    lh_x = S16(vl->x) - S16(vh->x);  lh_y = S16(vl->y) - S16(vh->y);
    hl_y = S16(vh->y) - S16(vl->y);
    hm_x = S16(vh->x) - S16(vm->x);
    lm_x = S16(vl->x) - S16(vm->x);  lm_y = S16(vl->y) - S16(vm->y);
    mh_x = clamp_s16(mh_x); mh_y = clamp_s16(mh_y);
    lh_x = clamp_s16(lh_x); lh_y = clamp_s16(lh_y);
    hl_y = clamp_s16(hl_y); hm_x = clamp_s16(hm_x);
    lm_x = clamp_s16(lm_x); lm_y = clamp_s16(lm_y);

    /* ---- cross product (lane 1):  MH.y*LH.x + LH.y*HM.x ---- */
    acc = p_udh(mh_y, lh_x);
    acc += p_udh(lh_y, hm_x);
    cross_i = (int32_t)((acc >> 32) & 0xffff);   /* vreadacc upper */
    cross_f = (int32_t)((acc >> 16) & 0xffff);   /* vreadacc middle */
    if (cross_i == 0 && cross_f == 0)
        return 0;                                  /* degenerate */

    /* ---- reciprocals ---- */
    inv_dy_lm_32 = rsp_rcp16(lm_y);
    {
        Rsp32 c;
        c.i = cross_i; c.f = cross_f;
        inv_cross = mk32(rsp_rcp32(
            (int32_t)(((uint32_t)U16(cross_i) << 16) | (uint32_t)U16(cross_f))));
        /* NR_temp = inv_cross * cross */
        nr = mac32(inv_cross, c, 0);
        /* NR = 16 - 16*NR : vmudh vOne*16 ; vmadn nr_f*-16 ; vmadh nr_i*-16 */
        acc = p_udh(1, 16);
        acc += p_udn(nr.f, -16);
        nr.f = acc_clamp_low(acc);
        acc += p_udh(nr.i, -16);
        nr.i = acc_clamp_mid(acc);
        /* inv_dx = nr * inv_cross */
        inv_dx = mac32(nr, inv_cross, 0);
    }
    inv_dy_m_32 = rsp_rcp16(mh_y);
    inv_dy_l_32 = rsp_rcp16(lh_y);

    /* ---- inv_dy scaled by 32 ; dX scaled by 0x1000 ; dXdY ---- */
    {
        Rsp32 idy, dxv;
        int32_t dxs_i, dxs_f;
        int pass;
        for (pass = 0; pass < 3; pass++)
        {
            int32_t dx_lane, idy32;
            Rsp32 *out;
            if (pass == 0) { dx_lane = lm_x; idy32 = inv_dy_lm_32; out = &dxldy; }
            else if (pass == 1) { dx_lane = mh_x; idy32 = inv_dy_m_32; out = &dxmdy; }
            else { dx_lane = lh_x; idy32 = inv_dy_l_32; out = &dxhdy; }
            idy = mk32(idy32);
            /* inv_dy_scaled: vmudl idy_f*32 ; vmadm idy_i*32 -> mid out (int),
             * vmadn -> low out (frac) */
            acc = p_udl(idy.f, idy_scale);
            acc += p_udm(idy.i, idy_scale);
            inv_dy_lm_sc.i = acc_clamp_mid(acc);
            inv_dy_lm_sc.f = acc_clamp_low(acc);
            /* dX_scaled: vmudm dX*0x1000 -> mid out (int), vmadn -> low (frac) */
            acc = p_udm(dx_lane, dx_scale);
            dxs_i = acc_clamp_mid(acc);
            dxs_f = acc_clamp_low(acc);
            /* dXdY = dX_scaled * inv_dy_scaled (canonical 32x32) */
            dxv.i = dxs_i; dxv.f = dxs_f;
            *out = mac32(dxv, inv_dy_lm_sc, 0);
        }
        (void)inv_dy_m_sc; (void)inv_dy_l_sc;
    }

    /* The slope fraction feeding the anchor walk is masked with the
     * microcode's v30[5] (the vand before the XH/XM accumulation):
     * 0xFFF8 on F3DZEX2 and revisions that share its constant vector.
     * The emitted dXdY command words stay unmasked. */
    dxldy_f2 = dxldy.f & frac_mask;
    dxmdy_f2 = dxmdy.f & frac_mask;
    dxhdy_f2 = dxhdy.f & frac_mask;
    (void)dxldy_f2;

    /* vcr clamp on the integer parts */
    dxldy.i = vcr_bound_clamp(dxldy.i, vcr_bound) & 0xffff;
    dxmdy.i = vcr_bound_clamp(dxmdy.i, vcr_bound) & 0xffff;
    dxhdy.i = vcr_bound_clamp(dxhdy.i, vcr_bound) & 0xffff;

    /* ---- y_spx = floor(H.y) - H.y in 15.16 (negated fraction) ---- */
    {
        int32_t frac = (U16(vh->y) * 0x4000) & 0xffff;  /* vmudn low */
        int borrow = (frac != 0);
        y_spx_f = (0 - frac) & 0xffff;
        y_spx_i = (0 - borrow) & 0xffff;
    }

    /* ---- XH / XM anchors: Pos.x<<14 + dXdY * y_spx ---- */
    {
        int pass;
        for (pass = 0; pass < 2; pass++)
        {
            Rsp32 *dd = pass ? &dxmdy : &dxhdy;
            int32_t f2 = pass ? dxmdy_f2 : dxhdy_f2;
            Rsp32 *out = pass ? &xm : &xh;
            acc = p_udn(0x4000, vh->x);                /* pos_h.x << 14 */
            acc += p_udl(f2, y_spx_f);
            acc += p_udm(dd->i, y_spx_f);
            acc += p_udn(f2, y_spx_i);
            out->f = acc_clamp_low(acc);
            acc += p_udh(dd->i, y_spx_i);
            out->i = acc_clamp_mid(acc);
        }
    }

    /* ---- lft flag: bit 7 of the cross product's high half ---- */
    lft = (cross_i >> 7) & 1;

    /* ---- per-vertex attribute lanes ---- */
    {
        const RspTriVtx *vv[3];
        int vi;
        vv[0] = vh; vv[1] = vm; vv[2] = vl;
        for (vi = 0; vi < 3; vi++)
        {
            const RspTriVtx *cv = smooth ? vv[vi] : v1c;
            at_i[vi][0] = cv->r;  at_i[vi][1] = cv->g;  at_i[vi][2] = cv->b;
            at_i[vi][3] = vv[vi]->a;
            at_f[vi][0] = 0x8000; at_f[vi][1] = 0x8000;
            at_f[vi][2] = 0x8000; at_f[vi][3] = 0x8000;
            at_i[vi][4] = 0; at_f[vi][4] = 0;
            at_i[vi][5] = 0; at_f[vi][5] = 0;
            at_i[vi][6] = 0; at_f[vi][6] = 0;
            at_i[vi][7] = (vv[vi]->z >> 16) & 0xffff;
            at_f[vi][7] = vv[vi]->z & 0xffff;
        }
    }

    /* ---- texture S/T/W attributes ---- */
    if (textured)
    {
        int32_t iw[3];
        int vi;
        iw[0] = vh->invw; iw[1] = vm->invw; iw[2] = vl->invw;
        max_iw = iw[0];
        if (iw[1] > max_iw) max_iw = iw[1];
        if (iw[2] > max_iw) max_iw = iw[2];
        inv_max = mk32(rsp_rcp32(max_iw));   /* raw reciprocal, no Newton */
        for (vi = 0; vi < 3; vi++)
        {
            Rsp32 w32 = mk32(iw[vi]);
            /* vmudm Wi*max_f ; vmadl Wf*max_f ; vmadn out_f Wf*max_i ;
             * vmadh out_i Wi*max_i */
            acc = p_udm(w32.i, inv_max.f);
            acc += p_udl(w32.f, inv_max.f);
            acc += p_udn(w32.f, inv_max.i);
            wnorm[vi].f = acc_clamp_low(acc);
            acc += p_udh(w32.i, inv_max.i);
            wnorm[vi].i = acc_clamp_mid(acc);
        }
        {
            const RspTriVtx *vv[3];
            vv[0] = vh; vv[1] = vm; vv[2] = vl;
            for (vi = 0; vi < 3; vi++)
            {
                int lane;
                for (lane = 4; lane <= 6; lane++)
                {
                    int32_t a16 = (lane == 4) ? vv[vi]->s
                                : (lane == 5) ? vv[vi]->t : 0x7fff;
                    /* vmudm a*wf ; vmadh a*wi -> int ; vmadn -> frac */
                    acc = p_udm(a16, wnorm[vi].f);
                    acc += p_udh(a16, wnorm[vi].i);
                    at_i[vi][lane] = acc_clamp_mid(acc);
                    at_f[vi][lane] = acc_clamp_low(acc);
                }
            }
        }
    }

    /* ---- attribute deltas and gradients ---- */
    for (k = 0; k < 8; k++)
    {
        Rsp32 a1, a2, a3;
        a1.i = at_i[0][k]; a1.f = at_f[0][k];
        a2.i = at_i[1][k]; a2.f = at_f[1][k];
        a3.i = at_i[2][k]; a3.f = at_f[2][k];
        dA_H[k] = sub32(a3, a1);
        dA_M[k] = sub32(a2, a1);

        /* dA_x = dMH.y * dA_H + dHL.y * dA_M  (vmudn/vmadh chains, raw
         * accumulator reads) */
        acc = p_udn(dA_H[k].f, mh_y);
        acc += p_udh(dA_H[k].i, mh_y);
        acc += p_udn(dA_M[k].f, hl_y);
        acc += p_udh(dA_M[k].i, hl_y);
        dA_x[k].i = (int32_t)((acc >> 32) & 0xffff);
        dA_x[k].f = (int32_t)((acc >> 16) & 0xffff);

        /* dA_y = dLH.x * dA_M + dHM.x * dA_H */
        acc = p_udn(dA_M[k].f, lh_x);
        acc += p_udh(dA_M[k].i, lh_x);
        acc += p_udn(dA_H[k].f, hm_x);
        acc += p_udh(dA_H[k].i, hm_x);
        dA_y[k].i = (int32_t)((acc >> 32) & 0xffff);
        dA_y[k].f = (int32_t)((acc >> 16) & 0xffff);

        /* dAdX = dA_x * inv_dx */
        dAdX[k] = mac32(dA_x[k], inv_dx, 0);

        /* dAdY = dA_y * inv_dx ; then the accumulator CONTINUES:
         * dAdE = dAdY + dAdX * dxhdy */
        dAdY[k] = mac32(dA_y[k], inv_dx, &acc);
        acc += p_udl(dAdX[k].f, dxhdy.f);
        acc += p_udm(dAdX[k].i, dxhdy.f);
        acc += p_udn(dAdX[k].f, dxhdy.i);
        dAdE[k].f = acc_clamp_low(acc);
        acc += p_udh(dAdX[k].i, dxhdy.i);
        dAdE[k].i = acc_clamp_mid(acc);

        /* base = attr(H) + dAdE * y_spx */
        acc = p_udn(at_f[0][k], 1);
        acc += p_udh(at_i[0][k], 1);
        acc += p_udl(dAdE[k].f, y_spx_f);
        acc += p_udm(dAdE[k].i, y_spx_f);
        acc += p_udn(dAdE[k].f, y_spx_i);
        base[k].f = acc_clamp_low(acc);
        acc += p_udh(dAdE[k].i, y_spx_i);
        base[k].i = acc_clamp_mid(acc);
    }

    /* The texture block's fourth halfwords are the z lane as stored by the
     * 8-byte sdv of lanes 4..7 -- captured before the z-block x32 scaling
     * mutates the lane. */
    pre_z_base  = base[7];
    pre_z_dAdX  = dAdX[7];
    pre_z_dAdE  = dAdE[7];
    pre_z_dAdY  = dAdY[7];

    /* ---- z block scaling (x32 with the special base sequence) ---- */
    if (z_buffered)
    {
        int32_t v10;
        /* $v10 = dAdE_f * y_spx_f (fresh accumulator, low read) */
        acc = p_udn(dAdE[7].f, y_spx_f);
        v10 = acc_clamp_low(acc);
        /* gradients * 32 (vmudn f*32 ; vmadh i*32) */
        for (k = 0; k < 1; k++) { }
        {
            Rsp32 *gr[3];
            int gi;
            gr[0] = &dAdE[7]; gr[1] = &dAdX[7]; gr[2] = &dAdY[7];
            for (gi = 0; gi < 3; gi++)
            {
                acc = p_udn(gr[gi]->f, 32);
                gr[gi]->f = acc_clamp_low(acc);
                acc += p_udh(gr[gi]->i, 32);
                gr[gi]->i = acc_clamp_mid(acc);
            }
        }
        /* base'' = (v10 * 32)>>16 + base * 32 :
         * vmudl v10*32 ; vmadn base_f*32 ; vmadh base_i*32 */
        acc = p_udl(v10, 32);
        acc += p_udn(base[7].f, 32);
        base[7].f = acc_clamp_low(acc);
        acc += p_udh(base[7].i, 32);
        base[7].i = acc_clamp_mid(acc);
    }

    /* ---- assemble command words ---- */
    {
        int32_t w0;
        int id = 0xc8 | (z_buffered ? 1 : 0) | 4 /* G_TRI_FILL base */
               | (textured ? 2 : 0);
        /* The microcode stores YL/YM/YH with plain 16-bit ssv: no 14-bit
         * masking, so negative values keep their full sign bits in the
         * command words (the RDP only decodes the low 14 bits). */
        w0 = (int32_t)(((uint32_t)id << 24)
           | ((uint32_t)lft << 23)
           | ((uint32_t)(level & 7) << 19)
           | ((uint32_t)(tile & 7) << 16)
           | ((uint32_t)vl->y & 0xffff));
        ew[0] = w0;
        ew[1] = (int32_t)((((uint32_t)vm->y & 0xffff) << 16)
              | ((uint32_t)vh->y & 0xffff));
        /* XL = M.x << 14 stored as one 32-bit word (int<<16 | frac) */
        ew[2] = (int32_t)((uint32_t)((int32_t)S16(vm->x) << 14));
        ew[3] = (int32_t)(((uint32_t)U16(dxldy.i) << 16) | (uint32_t)U16(dxldy.f));
        ew[4] = (int32_t)(((uint32_t)U16(xh.i) << 16) | (uint32_t)U16(xh.f));
        ew[5] = (int32_t)(((uint32_t)U16(dxhdy.i) << 16) | (uint32_t)U16(dxhdy.f));
        ew[6] = (int32_t)(((uint32_t)U16(xm.i) << 16) | (uint32_t)U16(xm.f));
        ew[7] = (int32_t)(((uint32_t)U16(dxmdy.i) << 16) | (uint32_t)U16(dxmdy.f));
        nw = 8;

        /* shade block */
        ew[nw + 0] = (int32_t)(((uint32_t)U16(base[0].i) << 16) | (uint32_t)U16(base[1].i));
        ew[nw + 1] = (int32_t)(((uint32_t)U16(base[2].i) << 16) | (uint32_t)U16(base[3].i));
        ew[nw + 2] = (int32_t)(((uint32_t)U16(dAdX[0].i) << 16) | (uint32_t)U16(dAdX[1].i));
        ew[nw + 3] = (int32_t)(((uint32_t)U16(dAdX[2].i) << 16) | (uint32_t)U16(dAdX[3].i));
        ew[nw + 4] = (int32_t)(((uint32_t)U16(base[0].f) << 16) | (uint32_t)U16(base[1].f));
        ew[nw + 5] = (int32_t)(((uint32_t)U16(base[2].f) << 16) | (uint32_t)U16(base[3].f));
        ew[nw + 6] = (int32_t)(((uint32_t)U16(dAdX[0].f) << 16) | (uint32_t)U16(dAdX[1].f));
        ew[nw + 7] = (int32_t)(((uint32_t)U16(dAdX[2].f) << 16) | (uint32_t)U16(dAdX[3].f));
        ew[nw + 8] = (int32_t)(((uint32_t)U16(dAdE[0].i) << 16) | (uint32_t)U16(dAdE[1].i));
        ew[nw + 9] = (int32_t)(((uint32_t)U16(dAdE[2].i) << 16) | (uint32_t)U16(dAdE[3].i));
        ew[nw +10] = (int32_t)(((uint32_t)U16(dAdY[0].i) << 16) | (uint32_t)U16(dAdY[1].i));
        ew[nw +11] = (int32_t)(((uint32_t)U16(dAdY[2].i) << 16) | (uint32_t)U16(dAdY[3].i));
        ew[nw +12] = (int32_t)(((uint32_t)U16(dAdE[0].f) << 16) | (uint32_t)U16(dAdE[1].f));
        ew[nw +13] = (int32_t)(((uint32_t)U16(dAdE[2].f) << 16) | (uint32_t)U16(dAdE[3].f));
        ew[nw +14] = (int32_t)(((uint32_t)U16(dAdY[0].f) << 16) | (uint32_t)U16(dAdY[1].f));
        ew[nw +15] = (int32_t)(((uint32_t)U16(dAdY[2].f) << 16) | (uint32_t)U16(dAdY[3].f));
        nw += 16;

        if (textured)
        {
            ew[nw + 0] = (int32_t)(((uint32_t)U16(base[4].i) << 16) | (uint32_t)U16(base[5].i));
            ew[nw + 1] = (int32_t)(((uint32_t)U16(base[6].i) << 16) | (uint32_t)U16(pre_z_base.i));
            ew[nw + 2] = (int32_t)(((uint32_t)U16(dAdX[4].i) << 16) | (uint32_t)U16(dAdX[5].i));
            ew[nw + 3] = (int32_t)(((uint32_t)U16(dAdX[6].i) << 16) | (uint32_t)U16(pre_z_dAdX.i));
            ew[nw + 4] = (int32_t)(((uint32_t)U16(base[4].f) << 16) | (uint32_t)U16(base[5].f));
            ew[nw + 5] = (int32_t)(((uint32_t)U16(base[6].f) << 16) | (uint32_t)U16(pre_z_base.f));
            ew[nw + 6] = (int32_t)(((uint32_t)U16(dAdX[4].f) << 16) | (uint32_t)U16(dAdX[5].f));
            ew[nw + 7] = (int32_t)(((uint32_t)U16(dAdX[6].f) << 16) | (uint32_t)U16(pre_z_dAdX.f));
            ew[nw + 8] = (int32_t)(((uint32_t)U16(dAdE[4].i) << 16) | (uint32_t)U16(dAdE[5].i));
            ew[nw + 9] = (int32_t)(((uint32_t)U16(dAdE[6].i) << 16) | (uint32_t)U16(pre_z_dAdE.i));
            ew[nw +10] = (int32_t)(((uint32_t)U16(dAdY[4].i) << 16) | (uint32_t)U16(dAdY[5].i));
            ew[nw +11] = (int32_t)(((uint32_t)U16(dAdY[6].i) << 16) | (uint32_t)U16(pre_z_dAdY.i));
            ew[nw +12] = (int32_t)(((uint32_t)U16(dAdE[4].f) << 16) | (uint32_t)U16(dAdE[5].f));
            ew[nw +13] = (int32_t)(((uint32_t)U16(dAdE[6].f) << 16) | (uint32_t)U16(pre_z_dAdE.f));
            ew[nw +14] = (int32_t)(((uint32_t)U16(dAdY[4].f) << 16) | (uint32_t)U16(dAdY[5].f));
            ew[nw +15] = (int32_t)(((uint32_t)U16(dAdY[6].f) << 16) | (uint32_t)U16(pre_z_dAdY.f));
            nw += 16;
        }
        if (z_buffered)
        {
            ew[nw + 0] = (int32_t)(((uint32_t)U16(base[7].i) << 16) | (uint32_t)U16(base[7].f));
            ew[nw + 1] = (int32_t)(((uint32_t)U16(dAdX[7].i) << 16) | (uint32_t)U16(dAdX[7].f));
            ew[nw + 2] = (int32_t)(((uint32_t)U16(dAdE[7].i) << 16) | (uint32_t)U16(dAdE[7].f));
            ew[nw + 3] = (int32_t)(((uint32_t)U16(dAdY[7].i) << 16) | (uint32_t)U16(dAdY[7].f));
            nw += 4;
        }
    }
    return nw;
}
