/* SPDX-License-Identifier: Apache-2.0
 * RTR-Bench - the one translation unit that holds the implementation of
 * miniaudio (third-party, a single header). Only devices are used: no
 * decoding, encoding, generators or engine. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
