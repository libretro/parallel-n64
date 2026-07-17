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
#include <stdlib.h>
#include <stdio.h>
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

    /* Normalize in unsigned arithmetic: the loop shifts the operand's
     * leading one up into bit 31, and doing that on a signed int is a
     * signed overflow the optimizer may assume away -- at -O2 the value-
     * range assumption perturbed the reciprocal, and with it every
     * attribute slope downstream, differently per surrounding codegen. */
    shift = 0;
    {
        uint32_t ua = (uint32_t)data;
        if (data != 0)
        {
            for (shift = 0; (ua & 0x80000000u) == 0u; ua <<= 1, shift++)
                ;
        }
        addr = (int32_t)((ua >> 22) & 0x1ffu);
    }
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

    /* Normalize in unsigned arithmetic: the loop shifts the operand's
     * leading one up into bit 31, and doing that on a signed int is a
     * signed overflow the optimizer may assume away -- at -O2 the value-
     * range assumption perturbed the reciprocal, and with it every
     * attribute slope downstream, differently per surrounding codegen. */
    shift = 0;
    {
        uint32_t ua = (uint32_t)data;
        if (data != 0)
        {
            for (shift = 0; (ua & 0x80000000u) == 0u; ua <<= 1, shift++)
                ;
        }
        addr = (int32_t)((ua >> 22) & 0x1ffu);
    }
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

/* (r32(a) * b64) >> 16 with mac32's int16:frac16 saturation, but the reciprocal
 * b carried in 64 bits.  On near-degenerate slivers the signed area collapses
 * toward zero and the int16:frac16 inv_dx saturates at ~2^31, capping every
 * gradient far below the LLE; widening the reciprocal lets the gradient reach
 * its correct magnitude (the final coefficient still fits int16:frac16). */
static Rsp32 mac32_wide(Rsp32 a, int64_t b64, RspAcc *acc_out)
{
    Rsp32 o;
    int64_t acc = ((int64_t)r32(a) * b64) >> 16;
    int64_t hi  = acc >> 16;
    if (hi < -32768)     { o.f = 0x0000; o.i = -32768 & 0xffff; }
    else if (hi > 32767) { o.f = 0xffff; o.i = 32767; }
    else                 { o.f = (int32_t)(acc & 0xffff); o.i = (int32_t)(hi & 0xffff); }
    if (acc_out) *acc_out = acc;
    return o;
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

/* ---- positional lighting (light_point) -------------------------------- */

/* Exported single directional dot for the positional-mode loop, which
 * dots one light per iteration instead of the two-per-round pairing of
 * lights_dircoloraccum2. */
int32_t rsp_light_dirdot(const int32_t n[3], const int32_t d[3])
{
    return rsp_light_dot(n, d);
}

/* One positional-loop color fold: vmulf(color, 0x7FFF) decays the running
 * <<7-domain color and vmacf adds one light's contribution. */
void rsp_light_fold1(int32_t lt[3], const int32_t rgb[3], int32_t d)
{
    int c;
    for (c = 0; c < 3; c++)
    {
        RspAcc acc = 2 * (RspAcc)S16(lt[c]) * 0x7fff + 0x8000;
        acc += 2 * (RspAcc)((rgb[c] & 0xff) << 7) * (RspAcc)S16(d);
        lt[c] = acc_read_signed(acc);
    }
}

/* The microcode's light_point chain, bit-exact. Per vertex and light:
 * the vertex goes to camera space through the modelview with the
 * vmudn/vmadh row sums and the vmadh mid clamp; the vertex-to-light
 * delta is the saturating vsub; the squared length is the vmudh squares
 * folded with the vaddc/vadd carry tree (the high adds saturate); vrsq
 * gives the reciprocal square root of that 32-bit value; the delta is
 * rotated back to model space with the modelview transpose and
 * normalized by the rsq pair through the early-writeback vmudm/vmadh
 * then the x4 vmudn/vmadh; the dot against the s8<<8 normal lanes runs
 * through the unsigned vmulu/vmacu clamp and the & 0x7FFF mask. For the
 * attenuation, vrcp of the rsq value recovers the length (only the low
 * half survives the register reuse), the quadratic input is the vmudh
 * clamp of len*16 squared down by vmudl, and the polynomial accumulates
 * as vmulf(len, kl) -- raw u8 kl, with the +0x8000 rounding bias --
 * + vmadm(q, kq<<5) + vmadn(kc<<4, 0x100); the 32-bit accumulator
 * reading is { vsar mid : vmadn low }. vrcp of that, masked to 0x7FFF,
 * scales the dot with a final vmulf. */
int32_t rsp_light_point_factor(const int32_t mv[4][4], const int32_t n[3],
                               const int32_t vtx[3], const int32_t pos[3],
                               int32_t kc, int32_t kl, int32_t kq)
{
    int32_t vc[3], d[3], dm[3], l[3];
    int32_t sq_mid[3], sq_hi[3];
    int32_t lo, si, carry, sum;
    int32_t rsq, len16, c16, q;
    int32_t att_lo, att_mid, att32, fpre, dot;
    Rsp32 r;
    RspAcc acc;
    int c, ax;

    for (c = 0; c < 3; c++)
    {
        acc  = p_udn(mv[3][c] & 0xffff, 1);
        acc += p_udh(mv[3][c] >> 16,    1);
        for (ax = 0; ax < 3; ax++)
        {
            acc += p_udn(mv[ax][c] & 0xffff, vtx[ax]);
            acc += p_udh(mv[ax][c] >> 16,    vtx[ax]);
        }
        vc[c] = acc_clamp_mid(acc);
    }

    for (c = 0; c < 3; c++)
        d[c] = clamp_s16(S16(pos[c]) - S16(vc[c]));

    for (c = 0; c < 3; c++)
    {
        acc = p_udh(d[c], d[c]);
        sq_mid[c] = (int32_t)((acc >> 16) & 0xffff);
        sq_hi[c]  = (int32_t)((acc >> 32) & 0xffff);
    }
    /* The vaddc/vadd pair tree: the [0q] round adds lane 0 into lane 1
     * and -- because the quarter pattern maps lane 2 onto itself --
     * doubles the z square; the [2h] round then folds the doubled lane 2
     * into lane 1. The "squared length" the microcode normalizes and
     * attenuates against is therefore dx*dx + dy*dy + 2*dz*dz, with dz in
     * camera space. This is the well-known F3DZEX point-light distance
     * bug; console-accurate output requires reproducing it. */
    {
        int32_t lo2, si2;
        sum = U16(sq_mid[0]) + U16(sq_mid[1]);
        carry = (sum >> 16) & 1;
        lo = sum & 0xffff;
        si = clamp_s16(S16(sq_hi[0]) + S16(sq_hi[1]) + carry);
        sum = U16(sq_mid[2]) + U16(sq_mid[2]);
        carry = (sum >> 16) & 1;
        lo2 = sum & 0xffff;
        si2 = clamp_s16(S16(sq_hi[2]) + S16(sq_hi[2]) + carry);
        sum = U16(lo) + U16(lo2);
        carry = (sum >> 16) & 1;
        lo = sum & 0xffff;
        si = clamp_s16(si + si2 + carry);
    }

    rsq = rsp_rsq32((int32_t)(((uint32_t)U16(si) << 16) | (uint32_t)U16(lo)));
    r = mk32(rsq);

    for (c = 0; c < 3; c++)
    {
        acc = 0;
        for (ax = 0; ax < 3; ax++)
        {
            acc += p_udn(mv[c][ax] & 0xffff, d[ax]);
            acc += p_udh(mv[c][ax] >> 16,    d[ax]);
        }
        dm[c] = acc_clamp_mid(acc);
    }

    for (c = 0; c < 3; c++)
    {
        int32_t v2i, v20i;
        acc = p_udm(dm[c], r.f);
        v2i = acc_clamp_mid(acc);
        acc += p_udh(dm[c], r.i);
        v20i = acc_clamp_mid(acc);
        acc  = p_udn(v2i, 4);
        acc += p_udh(v20i, 4);
        l[c] = acc_clamp_mid(acc);
    }

    acc = 0x8000;
    for (ax = 0; ax < 3; ax++)
        acc += 2 * (RspAcc)S16((n[ax] & 0xff) << 8) * (RspAcc)S16(l[ax]);
    dot = acc_read_unsigned(acc) & 0x7fff;

    len16 = (int32_t)(int16_t)((uint32_t)rsp_rcp32(rsq) & 0xffff);
    c16 = acc_clamp_mid(p_udh(len16, 0x10));
    q = acc_clamp_low(p_udl(c16, c16));
    acc  = 2 * (RspAcc)S16(len16) * (RspAcc)(kl & 0xff) + 0x8000;
    acc += p_udm(q, (kq & 0xff) << 5);
    acc += p_udn((kc & 0xff) << 4, 0x100);
    att_lo  = acc_clamp_low(acc);
    att_mid = (int32_t)((acc >> 16) & 0xffff);
    att32 = (int32_t)(((uint32_t)att_mid << 16) | (uint32_t)att_lo);
    fpre = (int32_t)((uint32_t)rsp_rcp32(att32) & 0x7fff);

    acc = 2 * (RspAcc)S16(fpre) * (RspAcc)S16(dot) + 0x8000;
    return acc_read_signed(acc);
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
/* Clip-lerp build selector: 0 = F3DEX2 2.05+/F3DZEX2 (vor 1 before the
 * sign-extraction vabs), 1 = the 2.04H build (raw sum). Chosen per task
 * from the microcode text (see the probe in rdp_emit_hle.c). */
static int s_clip_lerp_l3dex = 0;
static int s_clip_lerp_seed3 = 0;   /* the l3dex +3 accumulator seed */

void rsp_set_clip_lerp_l3dex(int on)
{
    s_clip_lerp_l3dex = on ? 1 : 0;
    s_clip_lerp_seed3 = on ? 1 : 0;
}

static int s_vtx_invw_2rd = 0;
static int s_tri_attr_rs = 0;

/* Rogue Squadron stale-lane residue (probe-anchored at the writer's
 * IMEM 0x1ac0 merge point): the z-disabled texture path only llv's the
 * S/T integer lanes, so the W lanes of the int registers and the L
 * vertex's fraction register keep whatever the last z-ENABLED triangle
 * computed there (transform residue before the first one). The H/M
 * S/T fraction lanes are rewritten every triangle by the edge
 * section's anchor back-walk (v10 = mids(clamped slope * y_spx),
 * IMEM 0x19d4..0x19e4); the T lanes pair with dxldy (H) and dxhdy (M),
 * while the S lanes multiply stale reciprocal registers and stay
 * unmodelled. */
static int32_t s_rs_stale_w_i[3] = { 0x7fff, 0x7fff, 0x7fff };
static int32_t s_rs_stale_w_f[3];
static int32_t s_rs_stale_l_sf, s_rs_stale_l_tf;

void rsp_set_tri_attr_rs(int on)
{
    s_tri_attr_rs = on ? 1 : 0;
}

void rsp_set_vtx_invw_2rd(int on)
{
    s_vtx_invw_2rd = on ? 1 : 0;
}

static int s_vtx_invw_raw = 0;

void rsp_set_vtx_invw_raw(int on)
{
    s_vtx_invw_raw = on ? 1 : 0;
}

static int s_vtx_y_round = 0;

/* Wipeout 64 stores vertex screen Y rounded to whole pixels (cxd4:
 * every stored y integral while x keeps quarter fractions; 122.5 -> 123,
 * 124.25 -> 124 -- round half up on the 10.2 value). */
void rsp_set_vtx_y_round(int on)
{
    s_vtx_y_round = on ? 1 : 0;
}

static int s_vtx_x_round = 0;

/* T3DUX (Turbo3D UX) stores vertex screen X rounded to whole pixels as
 * well: the cxd4 oracle's Last Legion UX streams carry not a single
 * fractional x or y lane in any triangle -- the Turbo3D family trades
 * sub-pixel precision for speed on both axes, where the F3DLX build
 * rounds only Y. */
void rsp_set_vtx_x_round(int on)
{
    s_vtx_x_round = on ? 1 : 0;
}

static int s_keep_degenerate = 0;

/* T3DUX: whole-pixel vertex quantization collapses slivers to a zero cross
 * product; the Turbo3D triangle writer runs its reciprocal on the zero
 * (VRCP(0) saturates) and emits the triangle rather than rejecting it, so
 * the F3DEX-style degenerate return is bypassed. */
void rsp_set_keep_degenerate(int on)
{
    s_keep_degenerate = on ? 1 : 0;
}

static int s_attr_lowp = 0;

/* T3DUX (Turbo3D UX) low-precision attribute coefficients: shade lanes
 * pure integer, shade and texture DaDy zero. */
void rsp_set_attr_lowp(int on)
{
    s_attr_lowp = on ? 1 : 0;
}

static int s_affine_tex = 0;

/* T3DUX (Turbo3D UX) textures affinely: the triangle write loads the raw
 * texel shorts with a constant 0x7fff W lane and no per-vertex perspective
 * normalizer -- every texture section in the oracle stream carries
 * W = 0x7fff with all dW slopes zero. */
void rsp_set_affine_tex(int on)
{
    s_affine_tex = on ? 1 : 0;
}

static int s_vtx_z_quant = 0;

/* T3DUX also stores vertex screen Z at reduced precision: every z base in
 * the oracle's triangle stream lands on a multiple of 0x0080 in the s15
 * integer lane (0x6800, 0x6780, 0x6980, ...), and small triangles carry
 * all-zero z slopes -- the per-vertex z is quantized to 0x80 steps before
 * the plane setup, so nearly-coplanar vertices collapse to flat z. */
void rsp_set_vtx_z_quant(int on)
{
    s_vtx_z_quant = on ? 1 : 0;
}

/* Wipeout 64's clip build: the l3dex two-rounding fold without the +3
 * accumulator seed (its overlay at text 0xf80 stages r' = r * (2 - r*d)
 * against the DMEM 0x60 constant row and never seeds the factor MAC). */
void rsp_set_clip_lerp_wo64(int on)
{
    if (on)
    {
        s_clip_lerp_l3dex = 1;
        s_clip_lerp_seed3 = 0;
    }
}

static int s_clip_lerp_204h = 0;

void rsp_set_clip_lerp_204h(int on)
{
    s_clip_lerp_204h = on ? 1 : 0;
}

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
        v8[L] = acc_clamp_low(acc);
        v9[L] = acc_clamp_mid(acc);    /* vmadh writes saturate */
        acc += p_udn(off, ncr) + p_udh(ofi, ncr);
        v10[L] = acc_clamp_low(acc);
        v11[L] = acc_clamp_mid(acc);
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
     * side. The 2.04H build feeds the raw sum, and the RSP vabs of a
     * zero sign operand is zero, not +2 -- the scaled reciprocal
     * collapses and the boundary vertex lands elsewhere. Tiny
     * perspNorm scales (Super Smash Bros. sends 8) make zero integer
     * sums routine, so model the build the microcode actually is. */
    if (s_clip_lerp_l3dex)
    {
        /* The line microcode stages the sign fold as two multiplies: an
         * unconditional x2 (vmudn/vmadh by $v31[2]) and, when the folded
         * denominator's scalar sign check (the bgez at text 0x990) sees
         * a negative sum, a second pass by $v31[3] = -1. Two clamp
         * roundings instead of the single +-2 multiply: the low halves
         * differ by an ulp on negative denominators, which is where the
         * remaining top-plane clip vertices sat. */
        acc = p_udn(rcp_lo, 2);
        rcp_lo = acc_clamp_low(acc);
        acc += p_udh(rcp_hi, 2);
        rcp_hi = acc_clamp_mid(acc);
        if (S16(v11[3]) < 0)
        {
            acc = p_udn(rcp_lo, -1);
            rcp_lo = acc_clamp_low(acc);
            acc += p_udh(rcp_hi, -1);
            rcp_hi = acc_clamp_mid(acc);
        }
    }
    else
    {
    if (s_clip_lerp_204h && S16(v11[3]) == 0)
        abs2 = 0;
    else if (S16(v11[3] | 1) > 0)  abs2 = 2;
    else                           abs2 = -2;
    acc = p_udn(rcp_lo, abs2);
    rcp_lo = acc_clamp_low(acc);
    acc += p_udh(rcp_hi, abs2);
    rcp_hi = acc_clamp_mid(acc);
    }
    /* veq/vmrg: keep the low half if the scaled high half is 0, else
     * saturate to 0xFFFF. */
    if (S16(rcp_hi) != 0)
        rcp_lo = 0xffff;
    /* (v11:v10) = diff * rcp ~= 1 */
    /* The Doom 64 line microcode seeds this accumulator with
     * vmudl $v31, $v31[5] before the multiply (text 0x9d8); on the
     * factor lane that is 0xFFFF * 4 >> 16 = 3, a three-ulp bias the
     * F3DEX2 chain does not have. It is what keeps a generated vertex
     * on the inside of the plane it was clipped against, which in turn
     * decides the stored quarter-pixel at a screen edge. */
    acc = s_clip_lerp_seed3 ? 3 : 0;
    acc += p_udl(v10[3], rcp_lo) + p_udm(v11[3], rcp_lo);
    v11[3] = acc_clamp_mid(acc);
    v10[3] = acc_clamp_low(acc);
    /* second reciprocal, of the ~1 value */
    {
        int32_t r = rsp_rcp32((int32_t)(((uint32_t)U16(v11[3]) << 16)
                                       | (uint32_t)U16(v10[3])));
        r2_lo = r & 0xffff;
        r2_hi = (r >> 16) & 0xffff;
    }
    if (s_clip_lerp_l3dex)
    {
        /* The Doom 64 line microcode's refinement routine (an overlay of
         * the text at 0xef8, DMA'd to IMEM 0 and reached by the clip's
         * jal): the second reciprocal is doubled up front and the
         * Newton-Raphson step is r' = r * (2 - r * v), staged as the
         * vmudn/vmadh x2 pair, the r * v mac chain, a 32-bit vsubc/vsub
         * against 2.0, and the final mac. Algebraically the same as the
         * F3DEX2 (4 - 4x) form below but the roundings differ within a
         * couple of ulps, which decides which side of a screen edge the
         * generated vertex re-projects to. */
        acc = p_udn(r2_lo, 2);
        r2_lo = acc_clamp_low(acc);
        acc += p_udh(r2_hi, 2);
        r2_hi = acc_clamp_mid(acc);
        acc = p_udl(r2_lo, v10[3]) + p_udm(r2_hi, v10[3]);
        acc += p_udn(r2_lo, v11[3]);
        x_lo = acc_clamp_low(acc);
        acc += p_udh(r2_hi, v11[3]);
        x_hi = acc_clamp_mid(acc);
        {
            int64_t dd = (int64_t)0x00020000 -
                (int64_t)(int32_t)(((uint32_t)U16(x_hi) << 16) |
                                   (uint32_t)U16(x_lo));
            x_lo = (int32_t)(dd & 0xffff);
            x_hi = (int32_t)((dd >> 16) & 0xffff);
        }
        acc = p_udl(r2_lo, x_lo) + p_udm(r2_hi, x_lo);
        acc += p_udn(r2_lo, x_hi);
        r2_lo = acc_clamp_low(acc);
        acc += p_udh(r2_hi, x_hi);
        r2_hi = acc_clamp_mid(acc);
    }
    else
    {
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
    }
    /* A * refined reciprocal, then * rcp again */
    acc = p_udl(v8[3], r2_lo) + p_udm(v9[3], r2_lo);
    acc += p_udn(v8[3], r2_hi);
    v10[3] = acc_clamp_low(acc);
    acc += p_udh(v9[3], r2_hi);
    v11[3] = acc_clamp_mid(acc);
    /* The Doom 64 line microcode seeds this accumulator with
     * vmudl $v31, $v31[5] before the multiply (text 0x9d8); on the
     * factor lane that is 0xFFFF * 4 >> 16 = 3, a three-ulp bias the
     * F3DEX2 chain does not have. It is what keeps a generated vertex
     * on the inside of the plane it was clipped against, which in turn
     * decides the stored quarter-pixel at a screen edge. */
    acc = s_clip_lerp_seed3 ? 3 : 0;
    acc += p_udl(v10[3], rcp_lo) + p_udm(v11[3], rcp_lo);
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
/* Rogue Squadron's vertex screen chain (live IMEM 0x162c..0x1778 with the
 * divide at 0x179c..0x17ec), modeled op for op:
 *   pw   = mid/low(w * perspNorm)                    (vmudl/vmadm, vmadn +0)
 *   r    = vrcph/vrcpl(pw), doubled through v30[2]==2 with the fraction
 *          re-latched after the integer term         (vmudn/vmadh/vmadn)
 *   t    = mac32(r, pw)
 *   u    = 2.0 - t                                   (vsubc/vsub, 2.0 from
 *                                                     the DMEM 0x50 row)
 *   iw   = mac32(r, u)
 *   iwc  = iw.i >= 0 ? iw.i : 0x7fff                 (vge/vmrg, v31[0])
 *   ndc  = mac32(clip, {iwc, iw.f})                  (raw fraction!)
 *   ndc2 = mid/low(ndc * perspNorm)
 *   scr  = vtrans + mid/low(ndc2 * vscale)           (vmudh/vmadn/vmadh,
 *          raw S13.2 viewport shorts, no negation)
 *   x,y clamped >= -4090 and z >= 0 on the integer lane only (vge against
 *   the DMEM 0x60 row through the 1q element pattern).
 * The stored 1/w is the UNCLAMPED iw (DMEM vertex +32/+34). */
/* Rogue Squadron per-vertex fog (transform tail, live IMEM 0x170c..
 * 0x173c): the perspective-divided, perspNorm-scaled z (the ndc2
 * vector's lane 2, probe-verified ~1.0 at the far plane) runs through
 * the DMEM 0x160 parameter row as clamp_k(0, mids(k * (z * m + o)))
 * and the clamped integer's low byte lands in the vertex record's fog
 * slot. m and o are 32-bit (lanes 0..3, m ~ -o ~ 1166 in the menu
 * task: a near/far ramp), k is the halfword in lane 4 (0xff). */
/* The most recent rsp_vtx_screen_rs call's perspective-divided,
 * perspNorm-scaled z (the ndc2 lane 2 the fog block consumes). */
static int32_t s_rs_last_ndc2z;
static int32_t s_rs_last_pw;

int32_t rsp_vtx_last_ndc2z(void)
{
    return s_rs_last_ndc2z;
}

int32_t rsp_vtx_last_pw(void)
{
    return s_rs_last_pw;
}

int32_t rsp_fog_rs(int32_t sz1616,
                   int32_t m_i, int32_t m_f,
                   int32_t o_i, int32_t o_f, int32_t k)
{
    Rsp32 z, mm, t;
    RspAcc acc;
    z.i = (sz1616 >> 16) & 0xffff; z.f = sz1616 & 0xffff;
    mm.i = m_i; mm.f = m_f;
    t = mac32(z, mm, 0);
    {
        /* vaddc/vadd 32-bit add */
        uint32_t lo = (uint32_t)U16(t.f) + (uint32_t)U16(o_f);
        int32_t carry = (lo > 0xffffu) ? 1 : 0;
        int32_t hi = S16(t.i) + S16(o_i) + carry;
        t.f = (int32_t)(lo & 0xffffu);
        if (hi > 32767) hi = 32767;
        if (hi < -32768) hi = -32768;
        t.i = hi;
    }
    acc = p_udn(t.f, k);
    t.f = acc_clamp_low(acc);
    acc += p_udh(t.i, k);
    t.i = acc_clamp_mid(acc);
    if (S16(t.i) < 0)
        t.i = 0;
    if (S16(t.i) > S16(k))
        t.i = k;
    return t.i & 0xff;
}

int rsp_vtx_screen_rs(int32_t cx, int32_t cy, int32_t cz, int32_t cw,
                      int32_t pn,
                      const int32_t *vs, const int32_t *vt,
                      int32_t *sx102, int32_t *sy102, int32_t *sz1616,
                      int32_t *invw_out)
{
    Rsp32 pos[3];
    Rsp32 pw, r, t, u, iw;
    int32_t iwc_i;
    int32_t scr_i[3], scr_f[3];
    RspAcc acc;
    int lane;

    /* The real transform runs unconditionally -- behind-the-eye
     * vertices still get a divide, an ndc2 (and thus a fog byte), and
     * stored screen fields; only this model's callers treat the screen
     * result as unusable. Bailing before the ndc2 computation left the
     * fog input stale for w <= 0 vertices, which the triangle fog
     * patch then read. */

    pos[0] = mk32(cx);
    pos[1] = mk32(cy);
    pos[2] = mk32(cz);

    /* perspNorm'd w */
    {
        Rsp32 w32 = mk32(cw);
        acc = p_udl(w32.f, pn);
        acc += p_udm(w32.i, pn);
        pw.i = acc_clamp_mid(acc);
        pw.f = acc_clamp_low(acc);
        s_rs_last_pw = (int32_t)(((uint32_t)U16(pw.i) << 16)
                                 | (uint32_t)U16(pw.f));
    }

    r = mk32(rsp_rcp32((int32_t)(((uint32_t)U16(pw.i) << 16)
                                 | (uint32_t)U16(pw.f))));

    /* double, fraction re-latched after the integer term */
    acc = p_udn(r.f, 2);
    acc += p_udh(r.i, 2);
    r.i = acc_clamp_mid(acc);
    r.f = acc_clamp_low(acc);

    /* Newton residual against 2.0 */
    t = mac32(r, pw, 0);
    {
        int32_t borrow = (U16(t.f) != 0) ? 1 : 0;
        u.f = (int32_t)((0 - U16(t.f)) & 0xffff);
        u.i = 2 - S16(t.i) - borrow;
        if (u.i > 32767) u.i = 32767;
        if (u.i < -32768) u.i = -32768;
    }
    iw = mac32(r, u, 0);

    if (invw_out)
        *invw_out = (int32_t)(((uint32_t)U16(iw.i) << 16)
                              | (uint32_t)U16(iw.f));

    iwc_i = (S16(iw.i) >= 0) ? iw.i : 0x7fff;

    for (lane = 0; lane < 3; lane++)
    {
        Rsp32 ndc, ndc2, iwm;
        iwm.i = iwc_i;
        iwm.f = iw.f;
        ndc = mac32(pos[lane], iwm, 0);

        acc = p_udl(ndc.f, pn);
        acc += p_udm(ndc.i, pn);
        ndc2.i = acc_clamp_mid(acc);
        ndc2.f = acc_clamp_low(acc);
        if (lane == 2)
            s_rs_last_ndc2z = (int32_t)(((uint32_t)U16(ndc2.i) << 16)
                                        | (uint32_t)U16(ndc2.f));

        acc = p_udh(vt[lane], 1);
        acc += p_udn(ndc2.f, vs[lane]);
        scr_f[lane] = acc_clamp_low(acc);
        acc += p_udh(ndc2.i, vs[lane]);
        scr_i[lane] = acc_clamp_mid(acc);
    }

    /* vge integer-lane clamps (DMEM 0x60 row, 1q pattern) */
    if (S16(scr_i[0]) < -4090) scr_i[0] = (int32_t)(-4090 & 0xffff);
    if (S16(scr_i[1]) < -4090) scr_i[1] = (int32_t)(-4090 & 0xffff);

    *sx102 = (int32_t)S16(scr_i[0]);
    *sy102 = (int32_t)S16(scr_i[1]);
    {
        int32_t zi = (int32_t)S16(scr_i[2]);
        if (zi < 0)
            zi = 0;
        *sz1616 = (int32_t)(((uint32_t)U16(zi) << 16) | (uint32_t)U16(scr_f[2]));
    }
    return (cw > 0) ? 1 : 0;
}

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
    if (s_vtx_invw_raw)
    {
        /* Turbo3D's reciprocal (gspTurbo3D text +0x538..+0x574): the
         * 32-bit vrcph/vrcpl table result is doubled through the
         * vmudn/vmadh constant multiply and used as-is -- there is no
         * Newton-Raphson feedback multiply at all (the microcode trades
         * vertex precision for speed). The refined F3DEX form lands
         * within one or two quarter-pixels of this, which is exactly
         * the jitter Dark Rift's fight-scene-init backdrop bake showed
         * against the LLE oracle. */
        acc = p_udn(r.f, 2);
        iw.f = acc_clamp_low(acc);
        acc += p_udh(r.i, 2);
        iw.i = acc_clamp_mid(acc);
    }
    else if (s_vtx_invw_2rd)
    {
        /* The Doom 64 line microcode's reciprocal routine (gspL3DEX text
         * 0xef8): the raw reciprocal is doubled first and the refinement
         * is r' = r * (2 - r * w), staged vmudn x2 / vmadh x2, the r * w
         * mac chain, a 32-bit vsubc/vsub against 2.0, and the final mac.
         * Off the clip boundary this agrees with the F3DEX2 (4 - 4x)
         * form, but the rounding differs within half an ulp of w' * r ==
         * 1, which is exactly where the clip lerp's generated vertices
         * land: a bottom-edge clip stores y 239.75 through this chain
         * and 240.0 through the other. */
        acc = p_udn(r.f, 2);
        r.f = acc_clamp_low(acc);
        acc += p_udh(r.i, 2);
        r.i = acc_clamp_mid(acc);
        t = mac32(r, pw, 0);
        {
            int64_t dd = (int64_t)0x00020000 -
                (int64_t)(int32_t)(((uint32_t)U16(t.i) << 16) |
                                   (uint32_t)U16(t.f));
            u.f = (int32_t)(dd & 0xffff);
            u.i = (int32_t)((dd >> 16) & 0xffff);
        }
        iw = mac32(r, u, 0);
    }
    else
    {
        t = mac32(pw, r, 0);
        acc = p_udh(1, 4);
        acc += p_udn(t.f, -4);
        u.f = acc_clamp_low(acc);
        acc += p_udh(t.i, -4);
        u.i = acc_clamp_mid(acc);
        iw = mac32(u, r, 0);
    }
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
    if (s_vtx_y_round)
    {
        /* Wipeout 64 stores vertex screen Y rounded half-up to whole
         * pixels (cxd4: every stored y integral -- 122.5 -> 123,
         * 124.25 -> 124 -- while x keeps its fractions). The 480-line
         * interlaced viewport renders each field at whole-line
         * granularity. */
        *sy102 = (*sy102 + 2) & ~3;
    }
    if (s_vtx_x_round)
    {
        /* T3DUX rounds screen X to whole pixels as well (same half-up on
         * the 10.2 value); no fractional x lane appears anywhere in the
         * oracle's Turbo3D UX triangle streams. */
        *sx102 = (*sx102 + 2) & ~3;
    }
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
        if (s_vtx_z_quant)
        {
            /* T3DUX: quantize the stored z to the nearest multiple of 4 in
             * the integer lane (fraction dropped) -- the low-precision
             * Turbo3D z store. The triangle write scales vertex z by 32
             * into the coefficient word, where these steps surface as the
             * 0x0080-aligned flat-z bases in the oracle stream. */
            *sz1616 = (int32_t)(((uint32_t)*sz1616 + 0x00020000u)
                                & 0xfffc0000u);
        }
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

/* DKR-family (F3DDKR / Jet Force Gemini / Mickey's Speedway) vertex fog.
 * Rare's vertex chain has no perspNorm and computes the projection with an
 * exact divide, so the fog input is the exact s15.16 ndc z = z/w rather than
 * the F3D reciprocal-chain product; the final lane shaping (fog_o + 0x7f00 +
 * ndc * fog_m, floored at 0x7f00, low byte) is the stock F3D one. Selected
 * against the cxd4 LLE stream on Jet Force Gemini's Goldwood landing frame:
 * the alpha population lands on the oracle's value clusters (0/100..126),
 * where the F3D chain under the default perspNorm computed factors several
 * times too small and the distance haze washed out. */
int32_t rsp_vtx_fog_dkr(int32_t cz, int32_t cw,
                        int32_t fog_m, int32_t fog_o)
{
    Rsp32 p2;
    RspAcc acc;
    int32_t lane, ndc;

    if (cw <= 0)
        return 0;
    ndc = (int32_t)(((int64_t)cz << 16) / cw);
    p2.i = (ndc >> 16) & 0xffff;
    p2.f = ndc & 0xffff;

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

/* Single-command line emitter: the L3DEX-family line microcodes draw each
 * gSPLine3D segment as one shade-triangle command in the YM==YL
 * parallelogram form -- two walked edges carrying the segment's own
 * VRCP-computed slope a constant (wd + 3) / 2 pixels apart -- rather than
 * a quad split into two triangles. The split's shared diagonal is walked
 * by two commands and its antialias coverage double-blends; the single
 * command has no interior edge. Coefficients transcribed from the
 * microcode streams of Doom 64's automap (every wall and player-arrow
 * stroke) and Blast Corps' J-Bomb trails:
 *   - endpoints sorted by y; YH/YL are the raw stored screen y's, YM = YL;
 *   - XH/XM anchor at (x_top -/+ (wd+3)/4 px) + dXdY * y_spx, the same
 *     subpixel anchor walk as the triangle write, with dXdY from the
 *     single-precision VRCP reciprocal of the y span;
 *   - a segment whose endpoints share a quantized scanline becomes the
 *     transposed form: a (wd+3)/2 px tall band between the raw endpoint
 *     x's in segment order, slopes zero, lft from the x order;
 *   - shade attributes are the top endpoint's integer colours with zero
 *     fractions (the raw path, like the 2D overlay quads); the gradients
 *     are DaDe == DaDy = delta * rcp(dy) per lane along the walk, with
 *     DrDx zero, and zero entirely for the transposed form;
 *   - the XL/DxL pair is never walked (zero rows) and is emitted as zero
 *     (Doom 64's build zeroes it; Blast Corps leaves stale DMEM there,
 *     which cannot be reproduced and renders identically). */
/* Append the 4-word z coefficient block of a z-buffered line: the top
 * endpoint's screen z as the base, the segment's z delta over its major
 * span as DzDe (and DzDy, mirroring the shade convention of the line
 * forms), and a zero DzDx: the line body is at most a few pixels wide,
 * so the across-line depth step is visually irrelevant while the
 * along-line interpolation carries the primitive's depth. Body
 * Harvest's line build interpolates all three on the RSP; this is the
 * linear approximation of its stream, not a transcription. */
static void rsp_line_z_block(int32_t *zw, int32_t zh, int32_t zl,
                             int32_t major_span_q)
{
    int32_t dzde = 0;
    if (major_span_q > 0)
        dzde = (int32_t)((((int64_t)zl - (int64_t)zh) * 4)
                         / (int64_t)major_span_q);
    zw[0] = zh;
    zw[1] = 0;             /* DzDx */
    zw[2] = dzde;          /* DzDe */
    zw[3] = dzde;          /* DzDy */
}

static int rsp_line_write_xmajor(int32_t *cmd,
                                 const RspTriVtx *vh, const RspTriVtx *vl,
                                 int width_q, int32_t dx_scale,
                                 int32_t idy_scale, int32_t slope_mask,
                                 int32_t *xl_dmem, int zbuf)
{
    int32_t h102 = (int32_t)(width_q + 3);          /* (wd+3)/4 px in 10.2 */
    int32_t yh102 = (int32_t)vh->y - h102;
    int32_t ym102 = (int32_t)vh->y + h102;
    int32_t yl102 = (int32_t)vl->y + h102;
    Rsp32 slope, xh;
    int lft;
    int32_t attr_i[4], dattr[4], dattr_x[4];
    int k;

    if (yl102 < 0)
        return 0;

    /* slope and attribute gradients. The edge step DrDe is the y-major
     * delta * rcp(dy) chain; the span step DrDx takes the second
     * reciprocal the microcode computes up front, rcp(dx); DrDy is
     * emitted zero in this form. */
    {
        int32_t dy102 = (int32_t)(int16_t)(vl->y - vh->y);
        int32_t dx102 = (int32_t)(int16_t)(vl->x - vh->x);
        int32_t idy32 = rsp_rcp16(dy102);
        int32_t idx32 = rsp_rcp16(dx102);
        Rsp32 idy_sc, idx_sc, dxv;
        RspAcc acc;
        Rsp32 rcp = mk32(idy32);
        Rsp32 rcx = mk32(idx32);
        acc = p_udl(rcp.f, idy_scale);
        acc += p_udm(rcp.i, idy_scale);
        idy_sc.i = acc_clamp_mid(acc);
        idy_sc.f = acc_clamp_low(acc);
        acc = p_udl(rcx.f, idy_scale);
        acc += p_udm(rcx.i, idy_scale);
        idx_sc.i = acc_clamp_mid(acc);
        idx_sc.f = acc_clamp_low(acc);
        acc = p_udm(dx102, dx_scale);
        dxv.i = acc_clamp_mid(acc);
        dxv.f = acc_clamp_low(acc);
        slope = mac32(dxv, idy_sc, 0);
        slope.f &= slope_mask;
        lft = dx102 < 0 ? 1 : 0;

        {
            int32_t dl[4];
            dl[0] = vl->r - vh->r; dl[1] = vl->g - vh->g;
            dl[2] = vl->b - vh->b; dl[3] = vl->a - vh->a;
            for (k = 0; k < 4; k++)
            {
                Rsp32 dv;
                dv.i = dl[k]; dv.f = 0;
                dattr[k]   = r32(mac32(dv, idy_sc, 0));
                dattr_x[k] = r32(mac32(dv, idx_sc, 0));
            }
        }
    }
    attr_i[0] = vh->r; attr_i[1] = vh->g; attr_i[2] = vh->b; attr_i[3] = vh->a;

    /* XH anchor walk from the top endpoint's x; attribute walk beside
     * it. A YH pushed negative by the width expansion is emitted raw --
     * the Y fields are signed and gspL3DEX's rotated-automap stream
     * carries -0.75 unclamped, with the anchor's subpixel walk taking
     * the negative coordinate's fraction as-is (unlike the y-major
     * form, which clamps a clipped-off top endpoint to the boundary). */
    {
        int32_t frac = (U16(yh102) * 0x4000) & 0xffff;
        int32_t y_spx_f = (0 - frac) & 0xffff;
        int32_t y_spx_i = (0 - (frac != 0 ? 1 : 0)) & 0xffff;
        RspAcc acc = p_udn(0x4000, (int32_t)vh->x);
        acc += p_udl(slope.f, y_spx_f);
        acc += p_udm(slope.i, y_spx_f);
        acc += p_udn(slope.f, y_spx_i);
        xh.f = acc_clamp_low(acc);
        acc += p_udh(slope.i, y_spx_i);
        xh.i = acc_clamp_mid(acc);
        if (frac)
        {
            for (k = 0; k < 4; k++)
            {
                int64_t v = ((int64_t)attr_i[k] << 16)
                          - (((int64_t)dattr[k] * (int64_t)frac) >> 16);
                attr_i[k] = (int32_t)(v >> 16) & 0xffff;
            }
        }
    }

    cmd[0] = (int32_t)((zbuf ? 0xCD000000u : 0xCC000000u)
                       | ((uint32_t)(lft & 1) << 23)
                       | ((uint32_t)yl102 & 0x3fffu));
    /* The microcode stores the YH halfword register raw: a negative
     * top -- the width expansion or a clipped vertex's cap extension --
     * carries its full 16-bit sign (the rotated and synthetic streams'
     * -0.75 tops are 0xFFFD, never 14-bit masked; the rasterizer sign
     * extends from bit 13 either way, so only the stream bytes differ). */
    cmd[1] = (int32_t)((((uint32_t)ym102 & 0x3fffu) << 16)
                       | ((uint32_t)U16(yh102)));
    cmd[2] = (int32_t)vh->x << 14;                     /* XL: band boundary */
    cmd[3] = (int32_t)((((uint32_t)U16(slope.i)) << 16) | (uint32_t)U16(slope.f));
    xl_dmem[0] = cmd[2];
    xl_dmem[1] = cmd[3];
    cmd[4] = (int32_t)((((uint32_t)U16(xh.i)) << 16) | (uint32_t)U16(xh.f));
    cmd[5] = cmd[3];
    cmd[6] = (int32_t)vh->x << 14;                     /* XM: vertical cap */
    cmd[7] = 0;
    cmd[8]  = (int32_t)((((uint32_t)attr_i[0] & 0xffffu) << 16) | ((uint32_t)attr_i[1] & 0xffffu));
    cmd[9]  = (int32_t)((((uint32_t)attr_i[2] & 0xffffu) << 16) | ((uint32_t)attr_i[3] & 0xffffu));
    cmd[10] = (int32_t)((((uint32_t)(dattr_x[0] >> 16) & 0xffffu) << 16)
                        | ((uint32_t)(dattr_x[1] >> 16) & 0xffffu));
    cmd[11] = (int32_t)((((uint32_t)(dattr_x[2] >> 16) & 0xffffu) << 16)
                        | ((uint32_t)(dattr_x[3] >> 16) & 0xffffu));
    cmd[12] = 0; cmd[13] = 0;
    cmd[14] = (int32_t)((((uint32_t)dattr_x[0] & 0xffffu) << 16)
                        | ((uint32_t)dattr_x[1] & 0xffffu));
    cmd[15] = (int32_t)((((uint32_t)dattr_x[2] & 0xffffu) << 16)
                        | ((uint32_t)dattr_x[3] & 0xffffu));
    cmd[16] = (int32_t)((((uint32_t)(dattr[0] >> 16) & 0xffffu) << 16)
                        | ((uint32_t)(dattr[1] >> 16) & 0xffffu));
    cmd[17] = (int32_t)((((uint32_t)(dattr[2] >> 16) & 0xffffu) << 16)
                        | ((uint32_t)(dattr[3] >> 16) & 0xffffu));
    cmd[18] = 0; cmd[19] = 0;                          /* DrDy: zero here */
    cmd[20] = (int32_t)((((uint32_t)dattr[0] & 0xffffu) << 16)
                        | ((uint32_t)dattr[1] & 0xffffu));
    cmd[21] = (int32_t)((((uint32_t)dattr[2] & 0xffffu) << 16)
                        | ((uint32_t)dattr[3] & 0xffffu));
    cmd[22] = 0; cmd[23] = 0;
    if (zbuf)
    {
        int32_t span = (int32_t)(int16_t)(vl->x - vh->x);
        if (span < 0) span = -span;
        rsp_line_z_block(cmd + 24, vh->z, vl->z, span);
        return 28;
    }
    return 24;
}

int rsp_line_write(int32_t *cmd, const RspTriVtx *e0, const RspTriVtx *e1,
                   int width_q, int32_t dx_scale, int32_t idy_scale,
                   int32_t slope_mask, int32_t *xl_dmem, int zbuf)
{
    const RspTriVtx *vh, *vl;
    int32_t half;                 /* (wd+3)/4 px in s15.16 */
    int32_t yh102, yl102;
    Rsp32 slope, xh, xm;
    int lft;
    int32_t attr_i[4], dattr[4];
    int k;

    half = (int32_t)(width_q + 3) << 14;

    if (e0->y == e1->y)
    {
        /* transposed: horizontal band. The microcode's XH edge and base
         * colour come from the command's second vertex operand (e1 here),
         * per its own stream. */
        int32_t y102 = e0->y;
        int32_t yh_q = ((y102 << 14) - half) >> 14;
        int32_t yl_q = ((y102 << 14) + half) >> 14;
        if (yl_q < 0)
            return 0;
        if (yh_q < 0)
            yh_q = 0;
        lft = (e1->x <= e0->x) ? 1 : 0;
        cmd[0] = (int32_t)((zbuf ? 0xCD000000u : 0xCC000000u)
                           | ((uint32_t)(lft & 1) << 23)
                           | ((uint32_t)yl_q & 0x3fffu));
        cmd[1] = (int32_t)((((uint32_t)yl_q & 0x3fffu) << 16)
                           | ((uint32_t)yh_q & 0x3fffu));
        /* XL edge: never walked; the command carries whatever the last
         * x-major line left in the DMEM slot (zero on a fresh task) */
        cmd[2] = xl_dmem[0]; cmd[3] = xl_dmem[1];
        cmd[4] = (int32_t)e1->x << 14; cmd[5] = 0;     /* XH */
        cmd[6] = (int32_t)e0->x << 14; cmd[7] = 0;     /* XM */
        cmd[8]  = (int32_t)((((uint32_t)e1->r & 0xffffu) << 16) | ((uint32_t)e1->g & 0xffffu));
        cmd[9]  = (int32_t)((((uint32_t)e1->b & 0xffffu) << 16) | ((uint32_t)e1->a & 0xffffu));
        for (k = 10; k < 24; k++)
            cmd[k] = 0;
        if (zbuf)
        {
            int32_t span = (int32_t)(int16_t)(e1->x - e0->x);
            if (span < 0) span = -span;
            rsp_line_z_block(cmd + 24, e1->z, e0->z, span);
            return 28;
        }
        return 24;
    }

    if (e0->y < e1->y) { vh = e0; vl = e1; }
    else               { vh = e1; vl = e0; }
    yh102 = vh->y; yl102 = vl->y;

    /* Majorness: a segment wider than tall walks x in the microcode and
     * is emitted in a third form (transcribed from the Blast Corps line
     * build's stream; not yet captured on gspL3DEX): the y span expanded
     * by (wd + 3) / 4 px on both ends (vertical end caps), YM at
     * y_top + h, the major XH edge anchor-walked from the top endpoint's
     * x at the segment slope, and the XL / XM edges at the raw top x --
     * XL carrying the slope (the band's other boundary, reaching the
     * bottom endpoint's x at YL) and XM vertical (the entry cap). Ties
     * stay y-major (a 45-degree J-Bomb segment appears in the y-major
     * form). */
    {
        int32_t dyv = (int32_t)(int16_t)(yl102 - yh102);
        int32_t dxv2 = (int32_t)(int16_t)(vl->x - vh->x);
        int32_t ady = dyv < 0 ? -dyv : dyv;
        int32_t adx = dxv2 < 0 ? -dxv2 : dxv2;
        if (adx > ady)
            return rsp_line_write_xmajor(cmd, vh, vl, width_q,
                                         dx_scale, idy_scale, slope_mask,
                                         xl_dmem, zbuf);
    }

    /* The 14-bit command Y fields cannot encode negative coordinates:
     * the microcode drops segments entirely above the screen and clips
     * a crossing segment's top to y = 0, leaving the anchor walked to
     * the boundary (verified on the automap's off-screen walls; only
     * the vertical case appears in the capture, where the anchor is
     * unchanged). The bottom and X directions stay with the scissor. */
    if (yl102 < 0)
        return 0;

    /* Slope: the single-precision VRCP reciprocal of the 10.2 y span,
     * scaled and multiplied through the same lane chain as the triangle
     * write, with the fraction masked by the microcode's v30 constant
     * 0xFFF8 before both the anchor walk and the emitted command words
     * (the -1.0 automap slopes land exact only when the mask follows the
     * signed multiply; the line microcode's own sequence at 0xb44..0xb78
     * of the Blast Corps build applies the same VAND). A handful of
     * Blast Corps slopes still differ from the microcode's stream in the
     * last masked fraction bit -- its multiply keeps a different
     * intermediate radix -- which is below a 1/8192-pixel edge offset. */
    {
        int32_t dy102 = (int32_t)(int16_t)(yl102 - yh102);
        int32_t dx102 = (int32_t)(int16_t)(vl->x - vh->x);
        int32_t idy32 = rsp_rcp16(dy102);
        Rsp32 idy_sc, dxv;
        RspAcc acc;
        int32_t dxs_i, dxs_f;
        Rsp32 rcp = mk32(idy32);
        acc = p_udl(rcp.f, idy_scale);
        acc += p_udm(rcp.i, idy_scale);
        idy_sc.i = acc_clamp_mid(acc);
        idy_sc.f = acc_clamp_low(acc);
        acc = p_udm(dx102, dx_scale);
        dxs_i = acc_clamp_mid(acc);
        dxs_f = acc_clamp_low(acc);
        dxv.i = dxs_i; dxv.f = dxs_f;
        slope = mac32(dxv, idy_sc, 0);
        slope.f &= slope_mask;

        /* per-lane attribute walk gradients: delta * rcp(dy) on the same
         * scaled reciprocal */
        /* Shade attributes: the base is the top endpoint's integer colour
         * walked to the first covered scanline by the same subpixel step
         * as the X anchor -- attr + DaDe * y_spx, truncated to the integer
         * with the fraction discarded (Blast Corps' fog-faded trail
         * alphas pin this: alpha 0x70 at y frac .5 with gradient +0.296
         * lands as 0x6f in the microcode's stream, fraction zero; Doom's
         * flat colours are walk-invariant). The gradient is
         * (bottom - top) * rcp(dy), unmasked; colour deltas are plain
         * units, not 10.2, so the reciprocal chain takes them undivided. */
        {
            int32_t dl[4];
            dl[0] = vl->r - vh->r; dl[1] = vl->g - vh->g;
            dl[2] = vl->b - vh->b; dl[3] = vl->a - vh->a;
            for (k = 0; k < 4; k++)
            {
                Rsp32 dv;
                dv.i = dl[k]; dv.f = 0;
                dattr[k] = r32(mac32(dv, idy_sc, 0));
            }
        }
        attr_i[0] = vh->r; attr_i[1] = vh->g; attr_i[2] = vh->b; attr_i[3] = vh->a;
    }

    /* subpixel anchor: y_spx = -(frac of vh->y), 15.16 lanes */
    {
        int32_t vh_y = yh102 < 0 ? 0 : (int32_t)vh->y;
        int32_t frac = (U16(vh_y) * 0x4000) & 0xffff;
        int32_t y_spx_f = (0 - frac) & 0xffff;
        int32_t y_spx_i = (0 - (frac != 0 ? 1 : 0)) & 0xffff;
        int pass;
        for (pass = 0; pass < 2; pass++)
        {
            int32_t base_x102 = (int32_t)vh->x;
            int32_t basex_i, basex_f;
            Rsp32 *out = pass ? &xm : &xh;
            RspAcc acc = p_udn(0x4000, base_x102);      /* x << 14 */
            if (yh102 < 0)
            {
                /* clip walk: x += slope * (0 - y_top) px */
                int64_t d = ((int64_t)r32(slope)
                             * (int64_t)((0 - yh102) << 14)) >> 16;
                acc += (RspAcc)d;
            }
            acc += p_udl(slope.f, y_spx_f);
            acc += p_udm(slope.i, y_spx_f);
            acc += p_udn(slope.f, y_spx_i);
            out->f = acc_clamp_low(acc);
            acc += p_udh(slope.i, y_spx_i);
            out->i = acc_clamp_mid(acc);
            /* -/+ half in 15.16 across the int:frac pair */
            basex_i = out->i; basex_f = out->f;
            {
                int64_t v = (((int64_t)(int16_t)basex_i) << 16) | (uint32_t)U16(basex_f);
                v += pass ? (int64_t)half : -(int64_t)half;
                out->i = (int32_t)((v >> 16) & 0xffff);
                out->f = (int32_t)(v & 0xffff);
            }
        }
    }
    lft = 1;

    /* attribute anchor walk (see above): integer part kept, fraction
     * discarded */
    {
        int32_t vh_y = yh102 < 0 ? 0 : (int32_t)vh->y;
        int32_t frac = (U16(vh_y) * 0x4000) & 0xffff;
        if (frac)
        {
            for (k = 0; k < 4; k++)
            {
                int64_t v = ((int64_t)attr_i[k] << 16)
                          - (((int64_t)dattr[k] * (int64_t)frac) >> 16);
                attr_i[k] = (int32_t)(v >> 16) & 0xffff;
            }
        }
        if (yh102 < 0)
        {
            for (k = 0; k < 4; k++)
            {
                int64_t v = ((int64_t)attr_i[k] << 16)
                          + (((int64_t)dattr[k]
                              * (int64_t)((0 - yh102) << 14)) >> 16);
                attr_i[k] = (int32_t)(v >> 16) & 0xffff;
            }
        }
    }

    {
        int32_t yh_emit = yh102 < 0 ? 0 : yh102;
        cmd[0] = (int32_t)((zbuf ? 0xCD000000u : 0xCC000000u)
                           | (1u << 23) | ((uint32_t)yl102 & 0x3fffu));
        cmd[1] = (int32_t)((((uint32_t)yl102 & 0x3fffu) << 16)
                           | ((uint32_t)yh_emit & 0x3fffu));
    }
    cmd[2] = xl_dmem[0]; cmd[3] = xl_dmem[1];     /* stale DMEM, not walked */
    cmd[4] = (int32_t)((((uint32_t)U16(xh.i)) << 16) | (uint32_t)U16(xh.f));
    cmd[5] = (int32_t)((((uint32_t)U16(slope.i)) << 16) | (uint32_t)U16(slope.f));
    cmd[6] = (int32_t)((((uint32_t)U16(xm.i)) << 16) | (uint32_t)U16(xm.f));
    cmd[7] = cmd[5];
    cmd[8]  = (int32_t)((((uint32_t)attr_i[0] & 0xffffu) << 16) | ((uint32_t)attr_i[1] & 0xffffu));
    cmd[9]  = (int32_t)((((uint32_t)attr_i[2] & 0xffffu) << 16) | ((uint32_t)attr_i[3] & 0xffffu));
    cmd[10] = 0; cmd[11] = 0;                          /* DrDx int */
    cmd[12] = 0; cmd[13] = 0;                          /* rgba frac */
    cmd[14] = 0; cmd[15] = 0;                          /* DrDx frac */
    cmd[16] = (int32_t)((((uint32_t)(dattr[0] >> 16) & 0xffffu) << 16)
                        | ((uint32_t)(dattr[1] >> 16) & 0xffffu));   /* DrDe int */
    cmd[17] = (int32_t)((((uint32_t)(dattr[2] >> 16) & 0xffffu) << 16)
                        | ((uint32_t)(dattr[3] >> 16) & 0xffffu));
    cmd[18] = cmd[16]; cmd[19] = cmd[17];              /* DrDy int */
    cmd[20] = (int32_t)((((uint32_t)dattr[0] & 0xffffu) << 16)
                        | ((uint32_t)dattr[1] & 0xffffu));           /* DrDe frac */
    cmd[21] = (int32_t)((((uint32_t)dattr[2] & 0xffffu) << 16)
                        | ((uint32_t)dattr[3] & 0xffffu));
    cmd[22] = cmd[20]; cmd[23] = cmd[21];              /* DrDy frac */
    if (zbuf)
    {
        int32_t span = (int32_t)(int16_t)(yl102 - yh102);
        if (span < 0) span = -span;
        rsp_line_z_block(cmd + 24, vh->z, vl->z, span);
        return 28;
    }
    return 24;
}

int rsp_tri_write(int32_t *ew,
                  const RspTriVtx *v1c, const RspTriVtx *v2c,
                  const RspTriVtx *v3c,
                  int textured, int z_buffered, int shaded, int smooth,
                  int tile, int level,
                  int32_t dx_scale, int32_t idy_scale,
                  int32_t frac_mask, int32_t vcr_bound)
{
    const RspTriVtx *vh, *vm, *vl, *tmpv;
    int32_t cross_i, cross_f;
    Rsp32 inv_cross, nr, inv_dx;
    int64_t inv_dx_64;
    int32_t inv_dy_m_32, inv_dy_l_32;     /* single-precision reciprocals */
    Rsp32 inv_dy_m_sc, inv_dy_l_sc, inv_dy_lm_sc;
    Rsp32 dxldy, dxmdy, dxhdy;
    int32_t dxldy_f2, dxmdy_f2, dxhdy_f2;
    int32_t y_spx_i, y_spx_f;
    Rsp32 xh, xm;
    int32_t max_iw;
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
    if (cross_i == 0 && cross_f == 0 && !s_keep_degenerate)
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
        inv_dx_64 = ((int64_t)r32(nr) * (int64_t)r32(inv_cross)) >> 16;
        if (s_tri_attr_rs)
        {
            /* Rogue Squadron routes the cross through its shared divide
             * (IMEM 0x179c, probe-verified ffff.ebb5 on the first drawn
             * triangle): the reciprocal is doubled through the v30[2]
             * constant with the fraction re-latched after the integer
             * term, then refined r' = r * (2 - r * x). The attribute
             * gradients multiply the numerators by THIS value. */
            Rsp32 rr, tt, uu;
            rr = mk32(rsp_rcp32(
                (int32_t)(((uint32_t)U16(cross_i) << 16)
                          | (uint32_t)U16(cross_f))));
            acc = p_udn(rr.f, 2);
            acc += p_udh(rr.i, 2);
            rr.i = acc_clamp_mid(acc);
            rr.f = acc_clamp_low(acc);
            tt = mac32(rr, c, 0);
            {
                int32_t borrow = (U16(tt.f) != 0) ? 1 : 0;
                uu.f = (int32_t)((0 - U16(tt.f)) & 0xffff);
                uu.i = 2 - S16(tt.i) - borrow;
                if (uu.i > 32767) uu.i = 32767;
                if (uu.i < -32768) uu.i = -32768;
            }
            inv_dx = mac32(rr, uu, 0);
        }
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
            /* The transform path carries the RSP's half-step colour bias;
             * the 2D overlay path loads integer colours with zero
             * fractions. */
            if (vv[vi]->flat2d)
            {
                at_f[vi][0] = 0; at_f[vi][1] = 0;
                at_f[vi][2] = 0; at_f[vi][3] = 0;
            }
            else
            {
                at_f[vi][0] = 0x8000; at_f[vi][1] = 0x8000;
                at_f[vi][2] = 0x8000; at_f[vi][3] = 0x8000;
            }
            at_i[vi][4] = 0; at_f[vi][4] = 0;
            at_i[vi][5] = 0; at_f[vi][5] = 0;
            at_i[vi][6] = 0; at_f[vi][6] = 0;
            at_i[vi][7] = (vv[vi]->z >> 16) & 0xffff;
            at_f[vi][7] = vv[vi]->z & 0xffff;
        }
    }

    /* ---- texture S/T/W attributes ---- */
    if (textured && (s_affine_tex
                     || (vh->flat2d && vm->flat2d && vl->flat2d)))
    {
        /* Fighting Force 64's 2D overlay path: the microcode's simplified
         * triangle writer loads the stored texel shorts and the 0x7fff W
         * lane directly, with no per-vertex perspective normalizer (its
         * quads are flat). Matching the normalized path here halves every
         * texture attribute; with the overlay's persp_tex_en off there is
         * no per-pixel division to cancel the halving and the glyphs
         * sample the wrong texels. */
        int vi;
        const RspTriVtx *vv[3];
        vv[0] = vh; vv[1] = vm; vv[2] = vl;
        for (vi = 0; vi < 3; vi++)
        {
            int lane;
            for (lane = 4; lane <= 6; lane++)
            {
                int32_t a16 = (lane == 4) ? vv[vi]->s
                            : (lane == 5) ? vv[vi]->t : 0x7fff;
                at_i[vi][lane] = a16;
                at_f[vi][lane] = 0;
            }
        }
    }
    else if (textured && s_tri_attr_rs && z_buffered)
    {
        /* Rogue Squadron's z-enabled perspective texture path (live
         * IMEM 0x1a04..0x1abc, probe- and stream-verified): the fold
         * at 0x1a04..0x1a2c takes the 32-bit MINIMUM of the three
         * vertices' perspNorm'd w values -- the divide INPUTS, not
         * recomputed reciprocals, so min(pw) == rcp(max invw) with no
         * reciprocal rounding -- halved through v31[13] == 0x8000.
         * Each vertex's normalizer is then the canonical 32-bit
         * multiply norm_v = invw_v * min(pw)/2 and the stored VTX_TC
         * shorts (plus an 0x7fff W seed placed by the vmov pair) are
         * scaled by norm_v with the vmudm/vmadh/vmadn mid/low latches,
         * per-vertex via quarter broadcasts. */
        int vi;
        const RspTriVtx *vv[3];
        Rsp32 half, mn;
        int32_t minw;
        vv[0] = vh; vv[1] = vm; vv[2] = vl;
        minw = vv[0]->pw;
        if (vv[1]->pw < minw) minw = vv[1]->pw;
        if (vv[2]->pw < minw) minw = vv[2]->pw;
        mn.i = (int32_t)(((uint32_t)minw >> 16) & 0xffffu);
        mn.f = (int32_t)((uint32_t)minw & 0xffffu);
        acc = p_udl(mn.f, 0x8000);
        acc += p_udm(mn.i, 0x8000);
        half.i = acc_clamp_mid(acc);
        half.f = acc_clamp_low(acc);
        for (vi = 0; vi < 3; vi++)
        {
            Rsp32 ivw, nrm;
            int k2;
            int32_t src[3];
            ivw.i = (int32_t)(((uint32_t)vv[vi]->invw >> 16) & 0xffffu);
            ivw.f = (int32_t)((uint32_t)vv[vi]->invw & 0xffffu);
            nrm = mac32(ivw, half, 0);
            src[0] = vv[vi]->s; src[1] = vv[vi]->t; src[2] = 0x7fff;
            for (k2 = 0; k2 < 3; k2++)
            {
                acc = p_udm(src[k2], nrm.f);
                acc += p_udh(src[k2], nrm.i);
                at_i[vi][4 + k2] = acc_clamp_mid(acc);
                at_f[vi][4 + k2] = acc_clamp_low(acc);
            }
            s_rs_stale_w_i[vi] = at_i[vi][6];
            s_rs_stale_w_f[vi] = at_f[vi][6];
        }
        s_rs_stale_l_sf = at_f[2][4];
        s_rs_stale_l_tf = at_f[2][5];
    }
    else if (textured && s_tri_attr_rs && !z_buffered)
    {
        /* Rogue Squadron's z-disabled (affine) texture path: the writer
         * loads the stored VTX_TC shorts -- which live in the DOUBLED
         * domain -- with no per-vertex perspective normalizer (the wnorm
         * block is branched around when geometry bit 0x1000 is set), and
         * the W lane keeps the 0x7fff seed. The stale-register residue
         * the real ucode leaks into the fraction lanes is not modelled. */
        int vi;
        const RspTriVtx *vv[3];
        int32_t yspx16 = (int32_t)((U16(vh->y) << 14) & 0xffff);
        vv[0] = vh; vv[1] = vm; vv[2] = vl;
        for (vi = 0; vi < 3; vi++)
        {
            at_i[vi][4] = vv[vi]->s; at_f[vi][4] = 0;
            at_i[vi][5] = vv[vi]->t; at_f[vi][5] = 0;
            at_i[vi][6] = s_rs_stale_w_i[vi];
            at_f[vi][6] = s_rs_stale_w_f[vi];
        }
        /* per-triangle anchor products in the H/M T fraction lanes */
        acc = p_udl(dxldy.f & frac_mask, yspx16);
        acc += p_udm(dxldy.i, yspx16);
        at_f[0][5] = acc_clamp_mid(acc);
        acc = p_udl(dxhdy.f & frac_mask, yspx16);
        acc += p_udm(dxhdy.i, yspx16);
        at_f[1][5] = acc_clamp_mid(acc);
        at_f[2][4] = s_rs_stale_l_sf;
        at_f[2][5] = s_rs_stale_l_tf;
    }
    else if (textured)
    {
        int32_t iw[3];
        int vi;
        iw[0] = vh->invw; iw[1] = vm->invw; iw[2] = vl->invw;
        max_iw = iw[0];
        if (iw[1] > max_iw) max_iw = iw[1];
        if (iw[2] > max_iw) max_iw = iw[2];
        for (vi = 0; vi < 3; vi++)
        {
            /* Shared per-vertex perspective normalizer.  The LLE RSP emits
             * floor(0x8000 * iw / max) here (equivalently the W lane becomes
             * floor(0x7fff * iw / (2*max))); the raw reciprocal-multiply path
             * biases this high by ~11 ulp, drifting the perspective gradient.
             * Form the quotient directly to match the LLE-emitted value. */
            int64_t pq = (int64_t)iw[vi] << 15;
            pq = (max_iw != 0) ? (pq / (int64_t)max_iw) : 0;
            wnorm[vi].f = (int32_t)(pq & 0xffff);
            wnorm[vi].i = (int32_t)((pq >> 16) & 0xffff);
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
    {
    int32_t rs_yspx_vh = (int32_t)U16(vh->y);
    int32_t rs_mh_y = mh_y, rs_hl_y = hl_y, rs_lh_x = lh_x, rs_hm_x = hm_x;
    if (s_tri_attr_rs)
    {
        /* The writer multiplies the packed edge deltas by v30[5] == 4
         * (vmudh, clamped mid) after the edge reciprocals but before the
         * attribute numerators (live IMEM 0x1978). */
        rs_mh_y = clamp_s16(mh_y * 4); rs_hl_y = clamp_s16(hl_y * 4);
        rs_lh_x = clamp_s16(lh_x * 4); rs_hm_x = clamp_s16(hm_x * 4);
    }
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
        acc = p_udn(dA_H[k].f, s_tri_attr_rs ? rs_mh_y : mh_y);
        acc += p_udh(dA_H[k].i, s_tri_attr_rs ? rs_mh_y : mh_y);
        acc += p_udn(dA_M[k].f, s_tri_attr_rs ? rs_hl_y : hl_y);
        acc += p_udh(dA_M[k].i, s_tri_attr_rs ? rs_hl_y : hl_y);
        dA_x[k].i = (int32_t)((acc >> 32) & 0xffff);
        dA_x[k].f = (int32_t)((acc >> 16) & 0xffff);

        /* dA_y = dLH.x * dA_M + dHM.x * dA_H */
        acc = p_udn(dA_M[k].f, s_tri_attr_rs ? rs_lh_x : lh_x);
        acc += p_udh(dA_M[k].i, s_tri_attr_rs ? rs_lh_x : lh_x);
        acc += p_udn(dA_H[k].f, s_tri_attr_rs ? rs_hm_x : hm_x);
        acc += p_udh(dA_H[k].i, s_tri_attr_rs ? rs_hm_x : hm_x);
        dA_y[k].i = (int32_t)((acc >> 32) & 0xffff);
        dA_y[k].f = (int32_t)((acc >> 16) & 0xffff);

        if (s_tri_attr_rs)
        {
            /* Rogue Squadron (live IMEM 0x1bc0..0x1ca0, probe-anchored):
             * gradients = the clamped canonical multiply of the vsar'd
             * numerators by the 0x179c-form reciprocal; dAdE on a fresh
             * accumulator; the base is a 32-bit SUBTRACT of the mid
             * slices of dAdE * the quarter-pixel fraction of the top
             * vertex's y ((y_H & 3) << 14, the low half of y * v30[4]
             * == 0x4000). */
            Rsp32 t;
            int32_t yspx16 = (int32_t)((U16(rs_yspx_vh) << 14) & 0xffff);
            dAdX[k] = mac32(dA_x[k], inv_dx, 0);
            dAdY[k] = mac32(dA_y[k], inv_dx, 0);
            acc = p_udn(dAdY[k].f, 1);
            acc += p_udh(dAdY[k].i, 1);
            acc += p_udl(dAdX[k].f, dxhdy.f);
            acc += p_udm(dAdX[k].i, dxhdy.f);
            acc += p_udn(dAdX[k].f, dxhdy.i);
            dAdE[k].f = acc_clamp_low(acc);
            acc += p_udh(dAdX[k].i, dxhdy.i);
            dAdE[k].i = acc_clamp_mid(acc);
            acc = p_udl(dAdE[k].f, yspx16);
            acc += p_udm(dAdE[k].i, yspx16);
            t.i = acc_clamp_mid(acc);
            t.f = acc_clamp_low(acc);
            {
                Rsp32 ah;
                ah.i = at_i[0][k]; ah.f = at_f[0][k];
                base[k] = sub32(ah, t);
            }
       }
        else
        {
        /* dAdX = dA_x * inv_dx */
        dAdX[k] = mac32_wide(dA_x[k], inv_dx_64, 0);

        /* dAdY = dA_y * inv_dx ; then the accumulator CONTINUES:
         * dAdE = dAdY + dAdX * dxhdy */
        dAdY[k] = mac32_wide(dA_y[k], inv_dx_64, &acc);
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
    }

    }

    if (s_attr_lowp)
    {
        /* T3DUX (Turbo3D UX) low-precision attribute profile, from the cxd4
         * oracle's Last Legion UX streams: every shade fraction lane (base,
         * DaDx, DaDe, DaDy) is zero in all emitted triangles -- the shade
         * coefficients are pure integer -- and the shade and texture DaDy
         * lanes are zero outright, integer and fraction. Only the z block
         * carries a live dy gradient. */
        for (k = 0; k < 8; k++)
        {
            if (k == 6)
                continue;   /* w lane: affine, already all-zero slopes */
            dAdY[k].i = 0;
            dAdY[k].f = 0;
            if (k < 4)
            {
                base[k].f = 0;
                dAdX[k].f = 0;
                dAdE[k].f = 0;
            }
        }
        /* The z DaDy is not the plane gradient either: 110 of the oracle's
         * 139 nonzero-slope triangles carry exactly zero, and the rest hold
         * small cross-lane residue (magnitude < 2 where the plane gradient
         * reaches +-25). Zero is the closest model; its render effect is
         * confined to the dz tolerance term of the z compare. */
    }

    /* The texture block's fourth halfwords are the z lane as stored by the
     * 8-byte sdv of lanes 4..7 -- captured before the z-block x32 scaling
     * mutates the lane. */
    pre_z_base  = base[7];
    pre_z_dAdX  = dAdX[7];
    pre_z_dAdE  = dAdE[7];
    pre_z_dAdY  = dAdY[7];

    /* ---- z block scaling (x32 with the special base sequence) ---- */
    if (z_buffered && s_tri_attr_rs)
    {
        /* Rogue Squadron's z block (live IMEM 0x1d3c..0x1da0,
         * probe-verified): the attribute pair and all three gradients
         * are first scaled into the <<5 domain (vmudn/vmadh by
         * v31[12] == 0x20), and the base subtract attrH - dAdE*y_spx
         * is then REDONE in that domain -- the product keeps five more
         * bits before its latch than the attribute-domain product, so
         * the base's low bits differ from (attr-domain base) << 5. */
        Rsp32 a5, t5;
        int32_t yspx16 = (int32_t)((U16(vh->y) << 14) & 0xffff);
        int gi5;
        Rsp32 *gr5[3];
        gr5[0] = &dAdE[7]; gr5[1] = &dAdX[7]; gr5[2] = &dAdY[7];
        for (gi5 = 0; gi5 < 3; gi5++)
        {
            acc = p_udn(gr5[gi5]->f, 32);
            gr5[gi5]->f = acc_clamp_low(acc);
            acc += p_udh(gr5[gi5]->i, 32);
            gr5[gi5]->i = acc_clamp_mid(acc);
        }
        acc = p_udn(at_f[0][7], 32);
        a5.f = acc_clamp_low(acc);
        acc += p_udh(at_i[0][7], 32);
        a5.i = acc_clamp_mid(acc);
        acc = p_udl(dAdE[7].f, yspx16);
        acc += p_udm(dAdE[7].i, yspx16);
        t5.i = acc_clamp_mid(acc);
        t5.f = acc_clamp_low(acc);
        base[7] = sub32(a5, t5);
    }
    else if (z_buffered)
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
        /* The command type mirrors the microcode's fill-mode byte:
         * G_TRI_FILL 0xc8 base, +1 Z, +2 texture, +4 shade -- the shade
         * bit follows G_SHADE in the geometry mode, not a constant. */
        int id = 0xc8 | (z_buffered ? 1 : 0) | (shaded ? 4 : 0)
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

        /* shade block: emitted only when G_SHADE is set (the microcode
         * computes the attribute lanes unconditionally and gates the
         * stores) */
        if (shaded)
        {
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
        }

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
