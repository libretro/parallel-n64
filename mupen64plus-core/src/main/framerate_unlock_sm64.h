/* AUTO-GENERATED — do not edit by hand.
 * Super Mario 64 (USA v1.0, sha1 9bef1128...) 60fps unlock word table.
 * Applying this to the stock USA v1.0 ROM reproduces the community
 * 'SM64 60fps V2' image byte-for-byte (verified by round-trip self-test).
 * 444 words.  Author: libretroadmin */

static const struct rompatch_word framerate_unlock_sm64_us_words[] = {
   { 0x000010u, 0x635a2bffu, 0xe5e9f5fbu, ROMPATCH_WF_NOVERIFY }, /* header CRC */
   { 0x000014u, 0x8b022326u, 0xe1c4551du, ROMPATCH_WF_NOVERIFY }, /* header CRC */
   { 0x00310cu, 0x0c0c8a00u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x003bc4u, 0x0c09218eu, 0x0c0bde49u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0064e8u, 0x0c09ec59u, 0x0c0dc3c0u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006cf4u, 0x12000006u, 0x12000005u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006cf8u, 0x00000000u, 0x24010001u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006cfcu, 0x24010001u, 0x12010006u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d00u, 0x12010008u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d04u, 0x00000000u, 0x10000009u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d08u, 0x1000000bu, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d0cu, 0x00000000u, 0x0c092ea3u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d10u, 0x0c092ea3u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d14u, 0x00000000u, 0x10000005u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d18u, 0xafa20024u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d1cu, 0x10000006u, 0x0c092e6eu, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d24u, 0x0c092e6eu, 0x0c0dc000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d2cu, 0xafa20024u, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d30u, 0x10000001u, 0x8fb00018u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d34u, 0x00000000u, 0x03e00008u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d38u, 0x10000003u, 0x27bd0028u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d3cu, 0x8fa20024u, 0x0c092e6eu, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d40u, 0x10000001u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d44u, 0x00000000u, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d48u, 0x8fbf001cu, 0x8fb00018u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d4cu, 0x8fb00018u, 0x03e00008u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d54u, 0x03e00008u, 0x27bd0028u, ROMPATCH_WF_NONE }, /* hook */
   { 0x006d58u, 0x00000000u, 0x00000001u, ROMPATCH_WF_NONE }, /* hook */
   { 0x00b7a4u, 0x29e10276u, 0x29e104ecu, ROMPATCH_WF_NONE }, /* hook */
   { 0x00f624u, 0x31ae0001u, 0x31ae0002u, ROMPATCH_WF_NONE }, /* hook */
   { 0x037e38u, 0xa5280004u, 0x0c0dc300u, ROMPATCH_WF_NONE }, /* hook */
   { 0x038744u, 0x11a0000eu, 0x1000000eu, ROMPATCH_WF_NONE }, /* hook */
   { 0x041918u, 0x0c0a239au, 0x0c0dc360u, ROMPATCH_WF_NONE }, /* hook */
   { 0x04195cu, 0x0c0a21c6u, 0x0c0dc340u, ROMPATCH_WF_NONE }, /* hook */
   { 0x041cc4u, 0x0c0a0562u, 0x0c0dc380u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0544f4u, 0x3c0e8034u, 0x3c0f8025u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0544f8u, 0x85cec75au, 0x91efbd5bu, ROMPATCH_WF_NONE }, /* hook */
   { 0x0544fcu, 0xa7ae0022u, 0x15e0037au, ROMPATCH_WF_NONE }, /* hook */
   { 0x054500u, 0x8faf0030u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x054504u, 0x91f80030u, 0x3c0e8034u, ROMPATCH_WF_NONE }, /* hook */
   { 0x054508u, 0xa3b8001fu, 0x85cdc75au, ROMPATCH_WF_NONE }, /* hook */
   { 0x05450cu, 0x3c198034u, 0xa7ad0022u, ROMPATCH_WF_NONE }, /* hook */
   { 0x054510u, 0x8739c84au, 0x8faf0030u, ROMPATCH_WF_NONE }, /* hook */
   { 0x054514u, 0x2401fffeu, 0x91f80030u, ROMPATCH_WF_NONE }, /* hook */
   { 0x054518u, 0x03214024u, 0xa3b8001fu, ROMPATCH_WF_NONE }, /* hook */
   { 0x05451cu, 0x3c018034u, 0x85d9c84au, ROMPATCH_WF_NONE }, /* hook */
   { 0x054520u, 0xa428c84au, 0x2401fffeu, ROMPATCH_WF_NONE }, /* hook */
   { 0x054524u, 0x3c098034u, 0x03214024u, ROMPATCH_WF_NONE }, /* hook */
   { 0x054528u, 0x8529c848u, 0xa5c8c84au, ROMPATCH_WF_NONE }, /* hook */
   { 0x05452cu, 0x2401feffu, 0x85c9c848u, ROMPATCH_WF_NONE }, /* hook */
   { 0x054530u, 0x01215024u, 0x2401feffu, ROMPATCH_WF_NONE }, /* hook */
   { 0x054534u, 0x3c018034u, 0x01215024u, ROMPATCH_WF_NONE }, /* hook */
   { 0x054538u, 0xa42ac848u, 0xa5cac848u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055320u, 0x1420000eu, 0x1420000au, ROMPATCH_WF_NONE }, /* hook */
   { 0x055324u, 0x00000000u, 0x87b80026u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055328u, 0x87b80026u, 0x2401ffffu, ROMPATCH_WF_NONE }, /* hook */
   { 0x05532cu, 0x2401ffffu, 0x13010004u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055330u, 0x13010006u, 0x3c198034u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055334u, 0x00000000u, 0x8739ca5cu, ROMPATCH_WF_NONE }, /* hook */
   { 0x055338u, 0x3c198034u, 0x0319082au, ROMPATCH_WF_NONE }, /* hook */
   { 0x05533cu, 0x8739ca5cu, 0x14200003u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055340u, 0x0319082au, 0x8fb90018u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055344u, 0x14200005u, 0x0320f809u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055348u, 0x00000000u, 0x8fa4001cu, ROMPATCH_WF_NONE }, /* hook */
   { 0x05534cu, 0x8fb90018u, 0x00001025u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055350u, 0x8fa4001cu, 0x8fbf0014u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055354u, 0x0320f809u, 0x03e00008u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055358u, 0x00000000u, 0x27bd0018u, ROMPATCH_WF_NONE }, /* hook */
   { 0x05535cu, 0x10000003u, 0x00001025u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055360u, 0x00001025u, 0x8fbf0014u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055364u, 0x10000001u, 0x03e00008u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055368u, 0x00000000u, 0x27bd0018u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055370u, 0x27bd0018u, 0x03e00008u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055374u, 0x03e00008u, 0x27bd0018u, ROMPATCH_WF_NONE }, /* hook */
   { 0x055378u, 0x00000000u, 0x27bd0018u, ROMPATCH_WF_NONE }, /* hook */
   { 0x095fd4u, 0x29210014u, 0x29210028u, ROMPATCH_WF_NONE }, /* hook */
   { 0x095fecu, 0x254b000au, 0x254b0005u, ROMPATCH_WF_NONE }, /* hook */
   { 0x095ffcu, 0x298100fbu, 0x292101f6u, ROMPATCH_WF_NONE }, /* hook */
   { 0x096014u, 0x25affff6u, 0x25affffbu, ROMPATCH_WF_NONE }, /* hook */
   { 0x096024u, 0x29c1010fu, 0x29c1021eu, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e984u, 0x24010708u, 0x24010e10u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e994u, 0x00000000u, 0x97a80028u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e998u, 0x97a80028u, 0x97b9002au, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e99cu, 0x97b9002au, 0x2401003cu, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9a0u, 0x2401001eu, 0x000848c0u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9a4u, 0x000848c0u, 0x01284823u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9a8u, 0x01284823u, 0x00094940u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9acu, 0x00094940u, 0x01284821u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9b0u, 0x01284821u, 0x00094900u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9b4u, 0x000948c0u, 0x03295023u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9b8u, 0x03295023u, 0x0141001au, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9bcu, 0x0141001au, 0x00005812u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9c0u, 0x00005812u, 0xa7ab0026u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9c4u, 0xa7ab0026u, 0x97ad0028u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9c8u, 0x00000000u, 0x97b80026u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9ccu, 0x97ad0028u, 0x97ac002au, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9d0u, 0x97b80026u, 0x000d70c0u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9d4u, 0x97ac002au, 0x01cd7023u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9d8u, 0x000d70c0u, 0x000e7140u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9dcu, 0x01cd7023u, 0x01cd7021u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9e0u, 0x000e7140u, 0x00184100u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9e4u, 0x01cd7021u, 0x000e7100u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9e8u, 0x00184100u, 0x01184023u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9ecu, 0x000e70c0u, 0x00084080u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9f0u, 0x01184023u, 0x018e7823u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9f4u, 0x00084040u, 0x01e8c823u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9f8u, 0x018e7823u, 0x3329ffffu, ROMPATCH_WF_NONE }, /* hook */
   { 0x09e9fcu, 0x01e8c823u, 0x24010006u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09ea00u, 0x3329ffffu, 0x0121001au, ROMPATCH_WF_NONE }, /* hook */
   { 0x09ea04u, 0x24010003u, 0x00005012u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09ea08u, 0x0121001au, 0xa7aa0024u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09ea0cu, 0x00005012u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x09ea10u, 0xa7aa0024u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2924u, 0x3c0e8036u, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2928u, 0x8dce1160u, 0xafbf001cu, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b292cu, 0x3c018038u, 0x3c088025u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2930u, 0x8dcf0154u, 0x910abd5bu, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2934u, 0xc5ca0168u, 0x394a0001u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2938u, 0x000fc280u, 0xa10abd5bu, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b293cu, 0x3319ffffu, 0x15400003u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2940u, 0x00194103u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2944u, 0x00084880u, 0x0c09218eu, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2948u, 0x00290821u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b294cu, 0xc4246000u, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2950u, 0x3c014120u, 0x03e00008u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0b2954u, 0x44813000u, 0x27bd0028u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0f00c0u, 0x0000020du, 0x00000834u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fb9f4u, 0x3c0f8039u, 0x3c0d8039u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fb9f8u, 0x8defbe28u, 0x8dafbe28u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fb9fcu, 0x3c018039u, 0x91ee0001u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fba00u, 0x91f80001u, 0x01ee7820u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fba04u, 0x01f8c821u, 0xadafbe28u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fba08u, 0xac39be28u, 0x3c048037u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fba0cu, 0x10000001u, 0x3c05007du, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fba10u, 0x00000000u, 0x0c09e141u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fba14u, 0x8fbf0014u, 0x34a64000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fba18u, 0x27bd0018u, 0x8fbf0014u, ROMPATCH_WF_NONE }, /* hook */
   { 0x0fba20u, 0x00000000u, 0x27bd0018u, ROMPATCH_WF_NONE }, /* hook */
   { 0x1028d0u, 0x3c0e8036u, 0x3c088036u, ROMPATCH_WF_NONE }, /* hook */
   { 0x1028d4u, 0x8dce1164u, 0x8d0e1164u, ROMPATCH_WF_NONE }, /* hook */
   { 0x1028e8u, 0x3c088036u, 0x8d081164u, ROMPATCH_WF_NONE }, /* hook */
   { 0x1028ecu, 0x8d081164u, 0x8d090000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x1028f0u, 0x8d090000u, 0x312affffu, ROMPATCH_WF_NONE }, /* hook */
   { 0x1028f4u, 0x312affffu, 0xa7aa0004u, ROMPATCH_WF_NONE }, /* hook */
   { 0x1028f8u, 0xa7aa0004u, 0x3c0b8033u, ROMPATCH_WF_NONE }, /* hook */
   { 0x1028fcu, 0x3c0b8033u, 0x8d6bd5d4u, ROMPATCH_WF_NONE }, /* hook */
   { 0x102900u, 0x8d6bd5d4u, 0x000b5842u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fb7cu, 0x11e10004u, 0x102f0004u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fb88u, 0x100000a6u, 0x100000a9u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fb98u, 0x170100a2u, 0x170100a5u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fe14u, 0x850899e8u, 0x850e99e8u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fe18u, 0x3c01801cu, 0x3c018025u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fe1cu, 0x250e0001u, 0x9021bd5bu, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fe20u, 0xa42e99e8u, 0x30210001u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fe24u, 0x10000003u, 0x01c17020u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fe28u, 0x8fa20048u, 0xa50e99e8u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fe2cu, 0x10000001u, 0x00000000u, ROMPATCH_WF_NONE }, /* hook */
   { 0x21fe30u, 0x00000000u, 0x8fa20048u, ROMPATCH_WF_NONE }, /* hook */
   { 0x2210a8u, 0x0c05c177u, 0x0c0dc38cu, ROMPATCH_WF_NONE }, /* hook */
   { 0x269eecu, 0x0304004bu, 0x03040096u, ROMPATCH_WF_NONE }, /* hook */
   { 0x7d0000u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0004u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0008u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d000cu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0010u, 0xffffffffu, 0x1540002eu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0014u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0018u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d001cu, 0xffffffffu, 0x3c088037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0020u, 0xffffffffu, 0x250900b8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0024u, 0xffffffffu, 0x8d0a1f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0028u, 0xffffffffu, 0xad0a2f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d002cu, 0xffffffffu, 0x25080004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0030u, 0xffffffffu, 0x1509fffcu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0034u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0038u, 0xffffffffu, 0x3c088037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d003cu, 0xffffffffu, 0x35091000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0040u, 0xffffffffu, 0x8d0a1000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0044u, 0xffffffffu, 0xad0a2000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0048u, 0xffffffffu, 0x8d0a1004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d004cu, 0xffffffffu, 0xad0a2004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0050u, 0xffffffffu, 0x8d0a1008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0054u, 0xffffffffu, 0xad0a2008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0058u, 0xffffffffu, 0x8d0a100cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d005cu, 0xffffffffu, 0xad0a200cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0060u, 0xffffffffu, 0x25080010u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0064u, 0xffffffffu, 0x1528fff6u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0068u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d006cu, 0xffffffffu, 0x3c088037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0070u, 0xffffffffu, 0x250900b8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0074u, 0xffffffffu, 0x3c0b8034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0078u, 0xffffffffu, 0x8d6ac698u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d007cu, 0xffffffffu, 0xad0a1f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0080u, 0xffffffffu, 0x25080004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0084u, 0xffffffffu, 0x1509fffcu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0088u, 0xffffffffu, 0x256b0004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d008cu, 0xffffffffu, 0x3c088034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0090u, 0xffffffffu, 0x2508d488u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0094u, 0xffffffffu, 0x3c098036u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0098u, 0xffffffffu, 0x35290e88u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d009cu, 0xffffffffu, 0x3c0b8037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00a0u, 0xffffffffu, 0x8d0a0074u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00a4u, 0xffffffffu, 0xad6a1000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00a8u, 0xffffffffu, 0x8d0a0020u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00acu, 0xffffffffu, 0xad6a1004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00b0u, 0xffffffffu, 0x8d0a0024u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00b4u, 0xffffffffu, 0xad6a1008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00b8u, 0xffffffffu, 0x8d0a0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00bcu, 0xffffffffu, 0xad6a100cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00c0u, 0xffffffffu, 0x25080260u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00c4u, 0xffffffffu, 0x1528fff6u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00c8u, 0xffffffffu, 0x256b0010u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00ccu, 0xffffffffu, 0x3c088037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00d0u, 0xffffffffu, 0x8d0a0ffcu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00d4u, 0xffffffffu, 0x254a0001u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00d8u, 0xffffffffu, 0xad0a0ffcu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00dcu, 0xffffffffu, 0x29410005u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00e0u, 0xffffffffu, 0x14200054u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00e4u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00e8u, 0xffffffffu, 0x3c0f8036u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00ecu, 0xffffffffu, 0x35ef0e88u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00f0u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00f4u, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00f8u, 0xffffffffu, 0x15400037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d00fcu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0100u, 0xffffffffu, 0x3c013f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0104u, 0xffffffffu, 0x44810000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0108u, 0xffffffffu, 0x3c088037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d010cu, 0xffffffffu, 0x25090030u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0110u, 0xffffffffu, 0x3c0b8034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0114u, 0xffffffffu, 0xc5021f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0118u, 0xffffffffu, 0xc50c2f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d011cu, 0xffffffffu, 0x460c1080u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0120u, 0xffffffffu, 0x46001082u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0124u, 0xffffffffu, 0xe562c698u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0128u, 0xffffffffu, 0x25080004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d012cu, 0xffffffffu, 0x1509fff9u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0130u, 0xffffffffu, 0x256b0004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0134u, 0xffffffffu, 0x3c013f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0138u, 0xffffffffu, 0x44810000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d013cu, 0xffffffffu, 0x3c088037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0140u, 0xffffffffu, 0x25080080u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0144u, 0xffffffffu, 0x25090038u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0148u, 0xffffffffu, 0x3c0b8034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d014cu, 0xffffffffu, 0x256b0080u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0150u, 0xffffffffu, 0xc5021f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0154u, 0xffffffffu, 0xc50c2f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0158u, 0xffffffffu, 0x460c1080u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d015cu, 0xffffffffu, 0x46001082u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0160u, 0xffffffffu, 0xe562c698u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0164u, 0xffffffffu, 0x25080004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0168u, 0xffffffffu, 0x1509fff9u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d016cu, 0xffffffffu, 0x256b0004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0170u, 0xffffffffu, 0x3c088034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0174u, 0xffffffffu, 0x2508d488u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0178u, 0xffffffffu, 0x3c0b8037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d017cu, 0xffffffffu, 0x85692000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0180u, 0xffffffffu, 0x11200010u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0184u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0188u, 0xffffffffu, 0xc5621004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d018cu, 0xffffffffu, 0xc56c2004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0190u, 0xffffffffu, 0x460c1080u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0194u, 0xffffffffu, 0x46001082u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0198u, 0xffffffffu, 0xe5020020u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d019cu, 0xffffffffu, 0xc5621008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01a0u, 0xffffffffu, 0xc56c2008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01a4u, 0xffffffffu, 0x460c1080u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01a8u, 0xffffffffu, 0x46001082u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01acu, 0xffffffffu, 0xe5020024u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01b0u, 0xffffffffu, 0xc562100cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01b4u, 0xffffffffu, 0xc56c200cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01b8u, 0xffffffffu, 0x460c1080u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01bcu, 0xffffffffu, 0x46001082u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01c0u, 0xffffffffu, 0xe5020028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01c4u, 0xffffffffu, 0x25080260u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01c8u, 0xffffffffu, 0x150fffecu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01ccu, 0xffffffffu, 0x256b0010u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01d0u, 0xffffffffu, 0x10000018u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01d4u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01d8u, 0xffffffffu, 0x3c088037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01dcu, 0xffffffffu, 0x250900b8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01e0u, 0xffffffffu, 0x3c0b8034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01e4u, 0xffffffffu, 0x8d0a1f00u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01e8u, 0xffffffffu, 0xad6ac698u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01ecu, 0xffffffffu, 0x25080004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01f0u, 0xffffffffu, 0x1509fffcu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01f4u, 0xffffffffu, 0x256b0004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01f8u, 0xffffffffu, 0x3c088034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d01fcu, 0xffffffffu, 0x2508d488u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0200u, 0xffffffffu, 0x3c0b8037u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0204u, 0xffffffffu, 0x85692000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0208u, 0xffffffffu, 0x11200007u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d020cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0210u, 0xffffffffu, 0x8d6a1004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0214u, 0xffffffffu, 0xad0a0020u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0218u, 0xffffffffu, 0x8d6a1008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d021cu, 0xffffffffu, 0xad0a0024u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0220u, 0xffffffffu, 0x8d6a100cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0224u, 0xffffffffu, 0xad0a0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0228u, 0xffffffffu, 0x25080260u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d022cu, 0xffffffffu, 0x150ffff5u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0230u, 0xffffffffu, 0x256b0010u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0234u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0238u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d023cu, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0b80u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0b84u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0b88u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0b8cu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0b90u, 0xffffffffu, 0x15400003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0b94u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0b98u, 0xffffffffu, 0x0c09f5bfu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0b9cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0ba0u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0ba4u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0ba8u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c00u, 0xffffffffu, 0x3c0f8025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c04u, 0xffffffffu, 0x91efbd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c08u, 0xffffffffu, 0x11e00020u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c0cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c10u, 0xffffffffu, 0x3c0c8034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c14u, 0xffffffffu, 0x8d8cc380u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c18u, 0xffffffffu, 0x1180001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c1cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c20u, 0xffffffffu, 0x00007825u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c24u, 0xffffffffu, 0x000c7402u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c28u, 0xffffffffu, 0x340a8006u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c2cu, 0xffffffffu, 0x15ca0005u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c30u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c34u, 0xffffffffu, 0x3c0f8036u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c38u, 0xffffffffu, 0x8def1158u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c3cu, 0xffffffffu, 0x95ef0048u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c40u, 0xffffffffu, 0x000f7842u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c44u, 0xffffffffu, 0x258cfffcu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c48u, 0xffffffffu, 0x958a0000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c4cu, 0xffffffffu, 0x3c0e8034u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c50u, 0xffffffffu, 0x95cec37au, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c54u, 0xffffffffu, 0x25ce0001u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c58u, 0xffffffffu, 0x01cf7020u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c5cu, 0xffffffffu, 0x01ca082au, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c60u, 0xffffffffu, 0x1020000au, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c64u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c68u, 0xffffffffu, 0x000f7840u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c6cu, 0xffffffffu, 0x032fc820u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c70u, 0xffffffffu, 0x872e0002u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c74u, 0xffffffffu, 0x310c8000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c78u, 0xffffffffu, 0x31d88000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c7cu, 0xffffffffu, 0x15980003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c80u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c84u, 0xffffffffu, 0x010e4020u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c88u, 0xffffffffu, 0x00084043u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c8cu, 0xffffffffu, 0xa5280004u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c90u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0c94u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d00u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d04u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d08u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d0cu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d10u, 0xffffffffu, 0x15400006u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d14u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d18u, 0xffffffffu, 0x0c0a21c6u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d1cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d20u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d24u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d28u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d2cu, 0xffffffffu, 0x1000fffdu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d30u, 0xffffffffu, 0x27ff0024u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d80u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d84u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d88u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d8cu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d90u, 0xffffffffu, 0x15400003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d94u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d98u, 0xffffffffu, 0x0c0a239au, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0d9cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0da0u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0da4u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0da8u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e00u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e04u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e08u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e0cu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e10u, 0xffffffffu, 0x15400003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e14u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e18u, 0xffffffffu, 0x0c0a0562u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e1cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e20u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e24u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e28u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e30u, 0xffffffffu, 0x03e01825u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e34u, 0xffffffffu, 0x3c018025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e38u, 0xffffffffu, 0x9021bd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e3cu, 0xffffffffu, 0x10200003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e40u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e44u, 0xffffffffu, 0x0c05c177u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e48u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e4cu, 0xffffffffu, 0x00600008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e50u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e80u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e84u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e88u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e8cu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e90u, 0xffffffffu, 0x15400003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e94u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e98u, 0xffffffffu, 0x0c09218eu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0e9cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0ea0u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0ea4u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0ea8u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f00u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f04u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f08u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f0cu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f10u, 0xffffffffu, 0x15400003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f14u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f18u, 0xffffffffu, 0x0c09ec59u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f1cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f20u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f24u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f28u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f2cu, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f30u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f80u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f84u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f88u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f8cu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f90u, 0xffffffffu, 0x15400003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f94u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f98u, 0xffffffffu, 0x0c0b2ab9u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0f9cu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fa0u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fa4u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fa8u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fc0u, 0xffffffffu, 0x27bdffd8u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fc4u, 0xffffffffu, 0xafbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fc8u, 0xffffffffu, 0x3c088025u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fccu, 0xffffffffu, 0x910abd5bu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fd0u, 0xffffffffu, 0x15400003u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fd4u, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fd8u, 0xffffffffu, 0x0c094e0eu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fdcu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fe0u, 0xffffffffu, 0x8fbf001cu, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fe4u, 0xffffffffu, 0x03e00008u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0fe8u, 0xffffffffu, 0x27bd0028u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   { 0x7d0ffcu, 0xffffffffu, 0x00000000u, ROMPATCH_WF_NONE }, /* new code (0xFF pad) */
   /* Fix: ACT_STAR_DANCE_NO_EXIT (0x1327) dead-end freeze after collecting a
    * star. The 60fps re-entry path leaves Mario in the cutscene-group action
    * 0x1327 whose handler (0x802598d0) has no exit, because the star-dance
    * animation frame is advanced past its loop end and is_anim_at_end() never
    * trips, so the dance never completes and Mario locks up (no input, no jump
    * sound). Overlay the handler's state dispatch with a direct
    * set_mario_action(m, ACT_IDLE, 0) so control is restored once Mario is in
    * this stuck action. Only action 0x1327 dispatches here (jump-table index
    * 0x27 -> 0x802598d0); the normal star dance (0x1302/0x1307 via
    * general_star_dance_handler at 0x80258184) is untouched. */
   { 0x0148fcu, 0x8fae0030u, 0x3c050c40u, ROMPATCH_WF_NONE }, /* lui  a1,0x0c40        */
   { 0x014900u, 0x95d00018u, 0x34a50201u, ROMPATCH_WF_NONE }, /* ori  a1,a1,0x201 (IDLE)*/
   { 0x014904u, 0x1200000cu, 0x8fa40030u, ROMPATCH_WF_NONE }, /* lw   a0,48(sp) (m)     */
   { 0x014908u, 0x00000000u, 0x0c0962c9u, ROMPATCH_WF_NONE }, /* jal  set_mario_action  */
   { 0x01490cu, 0x24010001u, 0x00003025u, ROMPATCH_WF_NONE }, /* move a2,zero (delay)   */
   { 0x014910u, 0x1201003du, 0x100000b8u, ROMPATCH_WF_NONE }, /* b    0x80259bf4        */
};
