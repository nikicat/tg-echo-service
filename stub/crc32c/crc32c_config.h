// Copyright 2017 The CRC32C Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef CRC32C_CRC32C_CONFIG_H_
#define CRC32C_CRC32C_CONFIG_H_

#define HAVE_BUILTIN_PREFETCH 1

#if defined(__x86_64__) || defined(_M_X64)
#define HAVE_SSE42 1
#define HAVE_MM_PREFETCH 1
#endif

#if defined(__aarch64__)
#define HAVE_ARM64_CRC32C 1
#endif

#define HAVE_STRONG_GETAUXVAL 1
#define HAVE_WEAK_GETAUXVAL

#endif  // CRC32C_CRC32C_CONFIG_H_
