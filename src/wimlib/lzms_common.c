/*
 * lzms_common.c - Common code for LZMS compression and decompression
 */

/*
 * Copyright (C) 2013-2016 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/cpu_features.h"
#include "wimlib/lzms_common.h"
#include "wimlib/unaligned.h"

#ifdef __x86_64__
#  include <emmintrin.h>
#endif

/* Table: offset slot => offset slot base value  */
const u32 lzms_offset_slot_base[LZMS_MAX_NUM_OFFSET_SYMS + 1] = {
	0x00000001, 0x00000002, 0x00000003, 0x00000004,
	0x00000005, 0x00000006, 0x00000007, 0x00000008,
	0x00000009, 0x0000000d, 0x00000011, 0x00000015,
	0x00000019, 0x0000001d, 0x00000021, 0x00000025,
	0x00000029, 0x0000002d, 0x00000035, 0x0000003d,
	0x00000045, 0x0000004d, 0x00000055, 0x0000005d,
	0x00000065, 0x00000075, 0x00000085, 0x00000095,
	0x000000a5, 0x000000b5, 0x000000c5, 0x000000d5,
	0x000000e5, 0x000000f5, 0x00000105, 0x00000125,
	0x00000145, 0x00000165, 0x00000185, 0x000001a5,
	0x000001c5, 0x000001e5, 0x00000205, 0x00000225,
	0x00000245, 0x00000265, 0x00000285, 0x000002a5,
	0x000002c5, 0x000002e5, 0x00000325, 0x00000365,
	0x000003a5, 0x000003e5, 0x00000425, 0x00000465,
	0x000004a5, 0x000004e5, 0x00000525, 0x00000565,
	0x000005a5, 0x000005e5, 0x00000625, 0x00000665,
	0x000006a5, 0x00000725, 0x000007a5, 0x00000825,
	0x000008a5, 0x00000925, 0x000009a5, 0x00000a25,
	0x00000aa5, 0x00000b25, 0x00000ba5, 0x00000c25,
	0x00000ca5, 0x00000d25, 0x00000da5, 0x00000e25,
	0x00000ea5, 0x00000f25, 0x00000fa5, 0x00001025,
	0x000010a5, 0x000011a5, 0x000012a5, 0x000013a5,
	0x000014a5, 0x000015a5, 0x000016a5, 0x000017a5,
	0x000018a5, 0x000019a5, 0x00001aa5, 0x00001ba5,
	0x00001ca5, 0x00001da5, 0x00001ea5, 0x00001fa5,
	0x000020a5, 0x000021a5, 0x000022a5, 0x000023a5,
	0x000024a5, 0x000026a5, 0x000028a5, 0x00002aa5,
	0x00002ca5, 0x00002ea5, 0x000030a5, 0x000032a5,
	0x000034a5, 0x000036a5, 0x000038a5, 0x00003aa5,
	0x00003ca5, 0x00003ea5, 0x000040a5, 0x000042a5,
	0x000044a5, 0x000046a5, 0x000048a5, 0x00004aa5,
	0x00004ca5, 0x00004ea5, 0x000050a5, 0x000052a5,
	0x000054a5, 0x000056a5, 0x000058a5, 0x00005aa5,
	0x00005ca5, 0x00005ea5, 0x000060a5, 0x000064a5,
	0x000068a5, 0x00006ca5, 0x000070a5, 0x000074a5,
	0x000078a5, 0x00007ca5, 0x000080a5, 0x000084a5,
	0x000088a5, 0x00008ca5, 0x000090a5, 0x000094a5,
	0x000098a5, 0x00009ca5, 0x0000a0a5, 0x0000a4a5,
	0x0000a8a5, 0x0000aca5, 0x0000b0a5, 0x0000b4a5,
	0x0000b8a5, 0x0000bca5, 0x0000c0a5, 0x0000c4a5,
	0x0000c8a5, 0x0000cca5, 0x0000d0a5, 0x0000d4a5,
	0x0000d8a5, 0x0000dca5, 0x0000e0a5, 0x0000e4a5,
	0x0000eca5, 0x0000f4a5, 0x0000fca5, 0x000104a5,
	0x00010ca5, 0x000114a5, 0x00011ca5, 0x000124a5,
	0x00012ca5, 0x000134a5, 0x00013ca5, 0x000144a5,
	0x00014ca5, 0x000154a5, 0x00015ca5, 0x000164a5,
	0x00016ca5, 0x000174a5, 0x00017ca5, 0x000184a5,
	0x00018ca5, 0x000194a5, 0x00019ca5, 0x0001a4a5,
	0x0001aca5, 0x0001b4a5, 0x0001bca5, 0x0001c4a5,
	0x0001cca5, 0x0001d4a5, 0x0001dca5, 0x0001e4a5,
	0x0001eca5, 0x0001f4a5, 0x0001fca5, 0x000204a5,
	0x00020ca5, 0x000214a5, 0x00021ca5, 0x000224a5,
	0x000234a5, 0x000244a5, 0x000254a5, 0x000264a5,
	0x000274a5, 0x000284a5, 0x000294a5, 0x0002a4a5,
	0x0002b4a5, 0x0002c4a5, 0x0002d4a5, 0x0002e4a5,
	0x0002f4a5, 0x000304a5, 0x000314a5, 0x000324a5,
	0x000334a5, 0x000344a5, 0x000354a5, 0x000364a5,
	0x000374a5, 0x000384a5, 0x000394a5, 0x0003a4a5,
	0x0003b4a5, 0x0003c4a5, 0x0003d4a5, 0x0003e4a5,
	0x0003f4a5, 0x000404a5, 0x000414a5, 0x000424a5,
	0x000434a5, 0x000444a5, 0x000454a5, 0x000464a5,
	0x000474a5, 0x000484a5, 0x000494a5, 0x0004a4a5,
	0x0004b4a5, 0x0004c4a5, 0x0004e4a5, 0x000504a5,
	0x000524a5, 0x000544a5, 0x000564a5, 0x000584a5,
	0x0005a4a5, 0x0005c4a5, 0x0005e4a5, 0x000604a5,
	0x000624a5, 0x000644a5, 0x000664a5, 0x000684a5,
	0x0006a4a5, 0x0006c4a5, 0x0006e4a5, 0x000704a5,
	0x000724a5, 0x000744a5, 0x000764a5, 0x000784a5,
	0x0007a4a5, 0x0007c4a5, 0x0007e4a5, 0x000804a5,
	0x000824a5, 0x000844a5, 0x000864a5, 0x000884a5,
	0x0008a4a5, 0x0008c4a5, 0x0008e4a5, 0x000904a5,
	0x000924a5, 0x000944a5, 0x000964a5, 0x000984a5,
	0x0009a4a5, 0x0009c4a5, 0x0009e4a5, 0x000a04a5,
	0x000a24a5, 0x000a44a5, 0x000a64a5, 0x000aa4a5,
	0x000ae4a5, 0x000b24a5, 0x000b64a5, 0x000ba4a5,
	0x000be4a5, 0x000c24a5, 0x000c64a5, 0x000ca4a5,
	0x000ce4a5, 0x000d24a5, 0x000d64a5, 0x000da4a5,
	0x000de4a5, 0x000e24a5, 0x000e64a5, 0x000ea4a5,
	0x000ee4a5, 0x000f24a5, 0x000f64a5, 0x000fa4a5,
	0x000fe4a5, 0x001024a5, 0x001064a5, 0x0010a4a5,
	0x0010e4a5, 0x001124a5, 0x001164a5, 0x0011a4a5,
	0x0011e4a5, 0x001224a5, 0x001264a5, 0x0012a4a5,
	0x0012e4a5, 0x001324a5, 0x001364a5, 0x0013a4a5,
	0x0013e4a5, 0x001424a5, 0x001464a5, 0x0014a4a5,
	0x0014e4a5, 0x001524a5, 0x001564a5, 0x0015a4a5,
	0x0015e4a5, 0x001624a5, 0x001664a5, 0x0016a4a5,
	0x0016e4a5, 0x001724a5, 0x001764a5, 0x0017a4a5,
	0x0017e4a5, 0x001824a5, 0x001864a5, 0x0018a4a5,
	0x0018e4a5, 0x001924a5, 0x001964a5, 0x0019e4a5,
	0x001a64a5, 0x001ae4a5, 0x001b64a5, 0x001be4a5,
	0x001c64a5, 0x001ce4a5, 0x001d64a5, 0x001de4a5,
	0x001e64a5, 0x001ee4a5, 0x001f64a5, 0x001fe4a5,
	0x002064a5, 0x0020e4a5, 0x002164a5, 0x0021e4a5,
	0x002264a5, 0x0022e4a5, 0x002364a5, 0x0023e4a5,
	0x002464a5, 0x0024e4a5, 0x002564a5, 0x0025e4a5,
	0x002664a5, 0x0026e4a5, 0x002764a5, 0x0027e4a5,
	0x002864a5, 0x0028e4a5, 0x002964a5, 0x0029e4a5,
	0x002a64a5, 0x002ae4a5, 0x002b64a5, 0x002be4a5,
	0x002c64a5, 0x002ce4a5, 0x002d64a5, 0x002de4a5,
	0x002e64a5, 0x002ee4a5, 0x002f64a5, 0x002fe4a5,
	0x003064a5, 0x0030e4a5, 0x003164a5, 0x0031e4a5,
	0x003264a5, 0x0032e4a5, 0x003364a5, 0x0033e4a5,
	0x003464a5, 0x0034e4a5, 0x003564a5, 0x0035e4a5,
	0x003664a5, 0x0036e4a5, 0x003764a5, 0x0037e4a5,
	0x003864a5, 0x0038e4a5, 0x003964a5, 0x0039e4a5,
	0x003a64a5, 0x003ae4a5, 0x003b64a5, 0x003be4a5,
	0x003c64a5, 0x003ce4a5, 0x003d64a5, 0x003de4a5,
	0x003ee4a5, 0x003fe4a5, 0x0040e4a5, 0x0041e4a5,
	0x0042e4a5, 0x0043e4a5, 0x0044e4a5, 0x0045e4a5,
	0x0046e4a5, 0x0047e4a5, 0x0048e4a5, 0x0049e4a5,
	0x004ae4a5, 0x004be4a5, 0x004ce4a5, 0x004de4a5,
	0x004ee4a5, 0x004fe4a5, 0x0050e4a5, 0x0051e4a5,
	0x0052e4a5, 0x0053e4a5, 0x0054e4a5, 0x0055e4a5,
	0x0056e4a5, 0x0057e4a5, 0x0058e4a5, 0x0059e4a5,
	0x005ae4a5, 0x005be4a5, 0x005ce4a5, 0x005de4a5,
	0x005ee4a5, 0x005fe4a5, 0x0060e4a5, 0x0061e4a5,
	0x0062e4a5, 0x0063e4a5, 0x0064e4a5, 0x0065e4a5,
	0x0066e4a5, 0x0067e4a5, 0x0068e4a5, 0x0069e4a5,
	0x006ae4a5, 0x006be4a5, 0x006ce4a5, 0x006de4a5,
	0x006ee4a5, 0x006fe4a5, 0x0070e4a5, 0x0071e4a5,
	0x0072e4a5, 0x0073e4a5, 0x0074e4a5, 0x0075e4a5,
	0x0076e4a5, 0x0077e4a5, 0x0078e4a5, 0x0079e4a5,
	0x007ae4a5, 0x007be4a5, 0x007ce4a5, 0x007de4a5,
	0x007ee4a5, 0x007fe4a5, 0x0080e4a5, 0x0081e4a5,
	0x0082e4a5, 0x0083e4a5, 0x0084e4a5, 0x0085e4a5,
	0x0086e4a5, 0x0087e4a5, 0x0088e4a5, 0x0089e4a5,
	0x008ae4a5, 0x008be4a5, 0x008ce4a5, 0x008de4a5,
	0x008fe4a5, 0x0091e4a5, 0x0093e4a5, 0x0095e4a5,
	0x0097e4a5, 0x0099e4a5, 0x009be4a5, 0x009de4a5,
	0x009fe4a5, 0x00a1e4a5, 0x00a3e4a5, 0x00a5e4a5,
	0x00a7e4a5, 0x00a9e4a5, 0x00abe4a5, 0x00ade4a5,
	0x00afe4a5, 0x00b1e4a5, 0x00b3e4a5, 0x00b5e4a5,
	0x00b7e4a5, 0x00b9e4a5, 0x00bbe4a5, 0x00bde4a5,
	0x00bfe4a5, 0x00c1e4a5, 0x00c3e4a5, 0x00c5e4a5,
	0x00c7e4a5, 0x00c9e4a5, 0x00cbe4a5, 0x00cde4a5,
	0x00cfe4a5, 0x00d1e4a5, 0x00d3e4a5, 0x00d5e4a5,
	0x00d7e4a5, 0x00d9e4a5, 0x00dbe4a5, 0x00dde4a5,
	0x00dfe4a5, 0x00e1e4a5, 0x00e3e4a5, 0x00e5e4a5,
	0x00e7e4a5, 0x00e9e4a5, 0x00ebe4a5, 0x00ede4a5,
	0x00efe4a5, 0x00f1e4a5, 0x00f3e4a5, 0x00f5e4a5,
	0x00f7e4a5, 0x00f9e4a5, 0x00fbe4a5, 0x00fde4a5,
	0x00ffe4a5, 0x0101e4a5, 0x0103e4a5, 0x0105e4a5,
	0x0107e4a5, 0x0109e4a5, 0x010be4a5, 0x010de4a5,
	0x010fe4a5, 0x0111e4a5, 0x0113e4a5, 0x0115e4a5,
	0x0117e4a5, 0x0119e4a5, 0x011be4a5, 0x011de4a5,
	0x011fe4a5, 0x0121e4a5, 0x0123e4a5, 0x0125e4a5,
	0x0127e4a5, 0x0129e4a5, 0x012be4a5, 0x012de4a5,
	0x012fe4a5, 0x0131e4a5, 0x0133e4a5, 0x0135e4a5,
	0x0137e4a5, 0x013be4a5, 0x013fe4a5, 0x0143e4a5,
	0x0147e4a5, 0x014be4a5, 0x014fe4a5, 0x0153e4a5,
	0x0157e4a5, 0x015be4a5, 0x015fe4a5, 0x0163e4a5,
	0x0167e4a5, 0x016be4a5, 0x016fe4a5, 0x0173e4a5,
	0x0177e4a5, 0x017be4a5, 0x017fe4a5, 0x0183e4a5,
	0x0187e4a5, 0x018be4a5, 0x018fe4a5, 0x0193e4a5,
	0x0197e4a5, 0x019be4a5, 0x019fe4a5, 0x01a3e4a5,
	0x01a7e4a5, 0x01abe4a5, 0x01afe4a5, 0x01b3e4a5,
	0x01b7e4a5, 0x01bbe4a5, 0x01bfe4a5, 0x01c3e4a5,
	0x01c7e4a5, 0x01cbe4a5, 0x01cfe4a5, 0x01d3e4a5,
	0x01d7e4a5, 0x01dbe4a5, 0x01dfe4a5, 0x01e3e4a5,
	0x01e7e4a5, 0x01ebe4a5, 0x01efe4a5, 0x01f3e4a5,
	0x01f7e4a5, 0x01fbe4a5, 0x01ffe4a5, 0x0203e4a5,
	0x0207e4a5, 0x020be4a5, 0x020fe4a5, 0x0213e4a5,
	0x0217e4a5, 0x021be4a5, 0x021fe4a5, 0x0223e4a5,
	0x0227e4a5, 0x022be4a5, 0x022fe4a5, 0x0233e4a5,
	0x0237e4a5, 0x023be4a5, 0x023fe4a5, 0x0243e4a5,
	0x0247e4a5, 0x024be4a5, 0x024fe4a5, 0x0253e4a5,
	0x0257e4a5, 0x025be4a5, 0x025fe4a5, 0x0263e4a5,
	0x0267e4a5, 0x026be4a5, 0x026fe4a5, 0x0273e4a5,
	0x0277e4a5, 0x027be4a5, 0x027fe4a5, 0x0283e4a5,
	0x0287e4a5, 0x028be4a5, 0x028fe4a5, 0x0293e4a5,
	0x0297e4a5, 0x029be4a5, 0x029fe4a5, 0x02a3e4a5,
	0x02a7e4a5, 0x02abe4a5, 0x02afe4a5, 0x02b3e4a5,
	0x02bbe4a5, 0x02c3e4a5, 0x02cbe4a5, 0x02d3e4a5,
	0x02dbe4a5, 0x02e3e4a5, 0x02ebe4a5, 0x02f3e4a5,
	0x02fbe4a5, 0x0303e4a5, 0x030be4a5, 0x0313e4a5,
	0x031be4a5, 0x0323e4a5, 0x032be4a5, 0x0333e4a5,
	0x033be4a5, 0x0343e4a5, 0x034be4a5, 0x0353e4a5,
	0x035be4a5, 0x0363e4a5, 0x036be4a5, 0x0373e4a5,
	0x037be4a5, 0x0383e4a5, 0x038be4a5, 0x0393e4a5,
	0x039be4a5, 0x03a3e4a5, 0x03abe4a5, 0x03b3e4a5,
	0x03bbe4a5, 0x03c3e4a5, 0x03cbe4a5, 0x03d3e4a5,
	0x03dbe4a5, 0x03e3e4a5, 0x03ebe4a5, 0x03f3e4a5,
	0x03fbe4a5, 0x0403e4a5, 0x040be4a5, 0x0413e4a5,
	0x041be4a5, 0x0423e4a5, 0x042be4a5, 0x0433e4a5,
	0x043be4a5, 0x0443e4a5, 0x044be4a5, 0x0453e4a5,
	0x045be4a5, 0x0463e4a5, 0x046be4a5, 0x0473e4a5,
	0x047be4a5, 0x0483e4a5, 0x048be4a5, 0x0493e4a5,
	0x049be4a5, 0x04a3e4a5, 0x04abe4a5, 0x04b3e4a5,
	0x04bbe4a5, 0x04c3e4a5, 0x04cbe4a5, 0x04d3e4a5,
	0x04dbe4a5, 0x04e3e4a5, 0x04ebe4a5, 0x04f3e4a5,
	0x04fbe4a5, 0x0503e4a5, 0x050be4a5, 0x0513e4a5,
	0x051be4a5, 0x0523e4a5, 0x052be4a5, 0x0533e4a5,
	0x053be4a5, 0x0543e4a5, 0x054be4a5, 0x0553e4a5,
	0x055be4a5, 0x0563e4a5, 0x056be4a5, 0x0573e4a5,
	0x057be4a5, 0x0583e4a5, 0x058be4a5, 0x0593e4a5,
	0x059be4a5, 0x05a3e4a5, 0x05abe4a5, 0x05b3e4a5,
	0x05bbe4a5, 0x05c3e4a5, 0x05cbe4a5, 0x05d3e4a5,
	0x05dbe4a5, 0x05e3e4a5, 0x05ebe4a5, 0x05f3e4a5,
	0x05fbe4a5, 0x060be4a5, 0x061be4a5, 0x062be4a5,
	0x063be4a5, 0x064be4a5, 0x065be4a5, 0x465be4a5,
	/* The last entry is extra; it is equal to LZMS_MAX_MATCH_OFFSET + 1 and
	 * is here to aid binary search.  */
};

/* Table: offset slot => number of extra offset bits  */
const u8 lzms_extra_offset_bits[LZMS_MAX_NUM_OFFSET_SYMS] = {
	0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 2 , 2 , 2 , 2 , 2 , 2 , 2 , 2,
	2 , 3 , 3 , 3 , 3 , 3 , 3 , 3 , 4 , 4 , 4 , 4 , 4 , 4 , 4 , 4,
	4 , 4 , 5 , 5 , 5 , 5 , 5 , 5 , 5 , 5 , 5 , 5 , 5 , 5 , 5 , 5,
	5 , 6 , 6 , 6 , 6 , 6 , 6 , 6 , 6 , 6 , 6 , 6 , 6 , 6 , 6 , 6,
	7 , 7 , 7 , 7 , 7 , 7 , 7 , 7 , 7 , 7 , 7 , 7 , 7 , 7 , 7 , 7,
	7 , 7 , 7 , 7 , 8 , 8 , 8 , 8 , 8 , 8 , 8 , 8 , 8 , 8 , 8 , 8,
	8 , 8 , 8 , 8 , 8 , 8 , 8 , 8 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9,
	9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9,
	9 , 9 , 9 , 9 , 9 , 9 , 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
	10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
	10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12,
	12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
	12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
	12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
	15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
	15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
	15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
	15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17,
	17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
	17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
	17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
	17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
	17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19,
	19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
	19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
	19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
	19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
	19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
	19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
	19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 30,
};

/* Table: length slot => length slot base value  */
const u32 lzms_length_slot_base[LZMS_NUM_LENGTH_SYMS + 1] = {
	0x00000001, 0x00000002, 0x00000003, 0x00000004,
	0x00000005, 0x00000006, 0x00000007, 0x00000008,
	0x00000009, 0x0000000a, 0x0000000b, 0x0000000c,
	0x0000000d, 0x0000000e, 0x0000000f, 0x00000010,
	0x00000011, 0x00000012, 0x00000013, 0x00000014,
	0x00000015, 0x00000016, 0x00000017, 0x00000018,
	0x00000019, 0x0000001a, 0x0000001b, 0x0000001d,
	0x0000001f, 0x00000021, 0x00000023, 0x00000027,
	0x0000002b, 0x0000002f, 0x00000033, 0x00000037,
	0x0000003b, 0x00000043, 0x0000004b, 0x00000053,
	0x0000005b, 0x0000006b, 0x0000007b, 0x0000008b,
	0x0000009b, 0x000000ab, 0x000000cb, 0x000000eb,
	0x0000012b, 0x000001ab, 0x000002ab, 0x000004ab,
	0x000008ab, 0x000108ab, 0x400108ab,
	/* The last entry is extra; it is equal to LZMS_MAX_MATCH_LENGTH + 1 and
	 * is here to aid binary search.  */
};

/* Table: length slot => number of extra length bits  */
const u8 lzms_extra_length_bits[LZMS_NUM_LENGTH_SYMS] = {
	0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
	0 , 0 , 1 , 1 , 1 , 1 , 2 , 2 ,
	2 , 2 , 2 , 2 , 3 , 3 , 3 , 3 ,
	4 , 4 , 4 , 4 , 4 , 5 , 5 , 6 ,
	7 , 8 , 9 , 10, 16, 30,
};

unsigned
lzms_get_slot(u32 value, const u32 slot_base_tab[], unsigned num_slots)
{
	unsigned l = 0;
	unsigned r = num_slots - 1;
	for (;;) {
		unsigned slot = (l + r) / 2;
		if (value >= slot_base_tab[slot]) {
			if (value < slot_base_tab[slot + 1])
				return slot;
			else
				l = slot + 1;
		} else {
			r = slot - 1;
		}
	}
}

/* Return the number of offset slots used when processing a buffer having the
 * specified uncompressed size.  */
unsigned
lzms_get_num_offset_slots(size_t uncompressed_size)
{
	if (uncompressed_size < 2)
		return 0;
	return 1 + lzms_get_offset_slot(uncompressed_size - 1);
}

void
lzms_init_probabilities(struct lzms_probabilites *probs)
{
	struct lzms_probability_entry *entries =
		(struct lzms_probability_entry *)probs;
	size_t num_entries = sizeof(struct lzms_probabilites) /
			     sizeof(struct lzms_probability_entry);
	for (size_t i = 0; i < num_entries; i++) {
		entries[i].num_recent_zero_bits = LZMS_INITIAL_PROBABILITY;
		entries[i].recent_bits = LZMS_INITIAL_RECENT_BITS;
	}
}

void
lzms_init_symbol_frequencies(u32 freqs[], unsigned num_syms)
{
	for (unsigned sym = 0; sym < num_syms; sym++)
		freqs[sym] = 1;
}

void
lzms_dilute_symbol_frequencies(u32 freqs[], unsigned num_syms)
{
	for (unsigned sym = 0; sym < num_syms; sym++)
		freqs[sym] = (freqs[sym] >> 1) + 1;
}


#ifdef __x86_64__
static forceinline u8 *
find_next_opcode_sse4_2(u8 *p)
{
	const __v16qi potential_opcodes = (__v16qi) {0x48, 0x4C, 0xE8, 0xE9, 0xF0, 0xFF};
	__asm__(
		"  pcmpestri $0x0, (%[p]), %[potential_opcodes]      \n"
		"  jc 2f                                             \n"
		"1:                                                  \n"
		"  add $0x10, %[p]                                   \n"
		"  pcmpestri $0x0, (%[p]), %[potential_opcodes]      \n"
		"  jnc 1b                                            \n"
		"2:                                                  \n"
	#ifdef __ILP32__ /* x32 ABI (x86_64 with 32-bit pointers) */
		"  add %%ecx, %[p]                                   \n"
	#else
		"  add %%rcx, %[p]                                   \n"
	#endif
		: [p] "+r" (p)
		: [potential_opcodes] "x" (potential_opcodes), "a" (6), "d" (16)
		: "rcx", "cc"
	       );

	return p;
}
#endif /* __x86_64__ */

static forceinline u8 *
find_next_opcode_default(u8 *p)
{
	/*
	 * The following table is used to accelerate the common case where the
	 * byte has nothing to do with x86 translation and must simply be
	 * skipped.  This was faster than the following alternatives:
	 *	- Jump table with 256 entries
	 *	- Switch statement with default
	 */
	static const u8 is_potential_opcode[256] = {
		[0x48] = 1, [0x4C] = 1, [0xE8] = 1,
		[0xE9] = 1, [0xF0] = 1, [0xFF] = 1,
	};

	for (;;) {
		if (is_potential_opcode[*p])
			break;
		p++;
		if (is_potential_opcode[*p])
			break;
		p++;
		if (is_potential_opcode[*p])
			break;
		p++;
		if (is_potential_opcode[*p])
			break;
		p++;
	}
	return p;
}

static forceinline u8 *
translate_if_needed(u8 *data, u8 *p, s32 *last_x86_pos,
		    s32 last_target_usages[], bool undo)
{
	s32 max_trans_offset;
	s32 opcode_nbytes;
	u16 target16;
	s32 i;

	max_trans_offset = LZMS_X86_MAX_TRANSLATION_OFFSET;

	/*
	 * p[0] has one of the following values:
	 *	0x48 0x4C 0xE8 0xE9 0xF0 0xFF
	 */

	if (p[0] >= 0xF0) {
		if (p[0] & 0x0F) {
			/* 0xFF (instruction group)  */
			if (p[1] == 0x15) {
				/* Call indirect relative  */
				opcode_nbytes = 2;
				goto have_opcode;
			}
		} else {
			/* 0xF0 (lock prefix)  */
			if (p[1] == 0x83 && p[2] == 0x05) {
				/* Lock add relative  */
				opcode_nbytes = 3;
				goto have_opcode;
			}
		}
	} else if (p[0] <= 0x4C) {

		/* 0x48 or 0x4C.  In 64-bit code this is a REX prefix byte with
		 * W=1, R=[01], X=0, and B=0, and it will be followed by the
		 * actual opcode, then additional bytes depending on the opcode.
		 * We are most interested in several common instructions that
		 * access data relative to the instruction pointer.  These use a
		 * 1-byte opcode, followed by a ModR/M byte, followed by a
		 * 4-byte displacement.  */

		/* Test: does the ModR/M byte indicate RIP-relative addressing?
		 * Note: there seems to be a mistake in the format here; the
		 * mask really should be 0xC7 instead of 0x07 so that both the
		 * MOD and R/M fields of ModR/M are tested, not just R/M.  */
		if ((p[2] & 0x07) == 0x05) {
			/* Check for the LEA (load effective address) or MOV
			 * (move) opcodes.  For MOV there are additional
			 * restrictions, although it seems they are only helpful
			 * due to the overly lax ModR/M test.  */
			if (p[1] == 0x8D ||
			    (p[1] == 0x8B && !(p[0] & 0x04) && !(p[2] & 0xF0)))
			{
				opcode_nbytes = 3;
				goto have_opcode;
			}
		}
	} else {
		if (p[0] & 0x01) {
			/* 0xE9: Jump relative.  Theoretically this would be
			 * useful to translate, but in fact it's explicitly
			 * excluded.  Most likely it creates too many false
			 * positives for the detection algorithm.  */
			p += 4;
		} else {
			/* 0xE8: Call relative.  This is a common case, so it
			 * uses a reduced max_trans_offset.  In other words, we
			 * have to be more confident that the data actually is
			 * x86 machine code before we'll do the translation.  */
			opcode_nbytes = 1;
			max_trans_offset >>= 1;
			goto have_opcode;
		}
	}

	return p + 1;

have_opcode:
	i = p - data;
	p += opcode_nbytes;
	if (undo) {
		if (i - *last_x86_pos <= max_trans_offset) {
			u32 n = get_unaligned_le32(p);
			put_unaligned_le32(n - i, p);
		}
		target16 = i + get_unaligned_le16(p);
	} else {
		target16 = i + get_unaligned_le16(p);
		if (i - *last_x86_pos <= max_trans_offset) {
			u32 n = get_unaligned_le32(p);
			put_unaligned_le32(n + i, p);
		}
	}

	i += opcode_nbytes + sizeof(le32) - 1;

	if (i - last_target_usages[target16] <= LZMS_X86_ID_WINDOW_SIZE)
		*last_x86_pos = i;

	last_target_usages[target16] = i;

	return p + sizeof(le32);
}

/*
 * Translate relative addresses embedded in x86 instructions into absolute
 * addresses (@undo == %false), or undo this translation (@undo == %true).
 *
 * Absolute addresses are usually more compressible by LZ factorization.
 *
 * @last_target_usages must be a temporary array of length >= 65536.
 */
void
lzms_x86_filter(u8* restrict data, s32 size,
		s32* restrict last_target_usages, bool undo)
{
	/*
	 * Note: this filter runs unconditionally and uses a custom algorithm to
	 * detect data regions that probably contain x86 code.
	 *
	 * 'last_x86_pos' tracks the most recent position that has a good chance
	 * of being the start of an x86 instruction.  When the filter detects a
	 * likely x86 instruction, it updates this variable and considers the
	 * next LZMS_X86_MAX_TRANSLATION_OFFSET bytes of data as valid for x86
	 * translations.
	 *
	 * If part of the data does not, in fact, contain x86 machine code, then
	 * 'last_x86_pos' will, very likely, eventually fall more than
	 * LZMS_X86_MAX_TRANSLATION_OFFSET bytes behind the current position.
	 * This results in x86 translations being disabled until the next likely
	 * x86 instruction is detected.
	 *
	 * To identify "likely x86 instructions", the algorithm attempts to
	 * track the position of the most recent potential relative-addressing
	 * instruction that referenced each possible memory address.  If it
	 * finds two references to the same memory address within an
	 * LZMS_X86_ID_WINDOW_SIZE-byte sized window, then the second reference
	 * is flagged as a likely x86 instruction.  Since the instructions
	 * considered for translation necessarily use relative addressing, the
	 * algorithm does a tentative translation into absolute addresses.  In
	 * addition, so that memory addresses can be looked up in an array of
	 * reasonable size (in this code, 'last_target_usages'), only the
	 * low-order 2 bytes of each address are considered significant.
	 */

	u8 *p;
	u8 *tail_ptr;
	s32 last_x86_pos = -LZMS_X86_MAX_TRANSLATION_OFFSET - 1;

	if (size <= 17)
		return;

	for (s32 i = 0; i < 65536; i++)
		last_target_usages[i] = -(s32)LZMS_X86_ID_WINDOW_SIZE - 1;

	/*
	 * Optimization: only check for end-of-buffer when we already have a
	 * byte that is a potential opcode for x86 translation.  To do this,
	 * overwrite one of the bytes near the end of the buffer, and restore it
	 * later.  The correctness of this optimization relies on two
	 * characteristics of compressed format:
	 *
	 *  1. No translation can follow an opcode beginning in the last 16
	 *     bytes.
	 *  2. A translation following an opcode starting at the last possible
	 *     position (17 bytes from the end) never extends more than 7 bytes.
	 *     Consequently, we can overwrite any of the bytes starting at
	 *     data[(size - 16) + 7] and have no effect on the result, as long
	 *     as we restore those bytes later.
	 */

	/* Note: the very first byte must be ignored completely!  */
	p = data + 1;
	tail_ptr = &data[size - 16];

#ifdef __x86_64__
	if (cpu_features & X86_CPU_FEATURE_SSE4_2) {
		u8 saved_byte = *tail_ptr;
		*tail_ptr = 0xE8;
		for (;;) {
			u8 *new_p = find_next_opcode_sse4_2(p);
			if (new_p >= tail_ptr - 8)
				break;
			p = new_p;
			p = translate_if_needed(data, p, &last_x86_pos,
						last_target_usages, undo);
		}
		*tail_ptr = saved_byte;
	}
#endif
	{
		u8 saved_byte = *(tail_ptr + 8);
		*(tail_ptr + 8) = 0xE8;
		for (;;) {
			p = find_next_opcode_default(p);
			if (p >= tail_ptr)
				break;
			p = translate_if_needed(data, p, &last_x86_pos,
						last_target_usages, undo);
		}
		*(tail_ptr + 8) = saved_byte;
	}
}
