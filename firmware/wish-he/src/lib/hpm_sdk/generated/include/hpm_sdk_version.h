/*
 * Copyright (c) 2022 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/*
 * NOTE: 이 파일은 원래 hpm_sdk 빌드 시 cmake configure_file() 로 생성된다
 *       (hpm_sdk_version.h.in + cmake/gen_version_h.cmake).
 *       본 프로젝트는 hpm_sdk 에 빌드 의존하지 않으므로 정적 사본으로 커밋한다.
 *
 *       vendored hpm_sdk : v1.12.1  (git describe: v1.12.1-3-g88b01b43900d)
 */

#ifndef HPM_SDK_VERSION_H
#define HPM_SDK_VERSION_H

/* #undef SDK_VERSION_CODE */
#define SDK_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

#define SDKVERSION          0x010C0100
#define SDK_VERSION_NUMBER  0x010C01
#define SDK_VERSION_MAJOR   1
#define SDK_VERSION_MINOR   12
#define SDK_PATCHLEVEL      1
#define SDK_VERSION_STRING  "1.12.1"

#define BUILD_VERSION          v1_12_1_3_g88b01b43900d


#endif /* HPM_SDK_VERSION_H */
