// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file framework.h
 * @brief Windows framework header configuration.
 *
 * Precompiled header support for Windows builds.
 * Excludes rarely-used Windows headers for faster compilation.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN             // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.
