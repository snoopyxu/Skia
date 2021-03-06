# Copyright 2014 Google Inc.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file is included by chrome's skia/skia_common.gypi, and is intended to
# augment the skia flags that are set there.

{
  'variables': {

    # These flags will be defined in the android framework.
    #
    # If these become 'permanent', they should be moved into common_variables.gypi
    #
    'skia_for_android_framework_defines': [
      'SK_SUPPORT_LEGACY_PUBLIC_IMAGEINFO_FIELDS',
      'SK_SUPPORT_LEGACY_ALLOCPIXELS_BOOL',
      'SK_SUPPORT_LEGACY_GETDEVICE',
      # Needed until we fix skbug.com/2440.
      'SK_SUPPORT_LEGACY_CLIPTOLAYERFLAG',
      # Transitional, for deprecated SkCanvas::SaveFlags methods.
      'SK_ATTR_DEPRECATED=SK_NOTHING_ARG1',
      'SK_LEGACY_PICTURE_SIZE_API',
      'SK_LEGACY_PICTURE_DRAW_API',
      'SK_LEGACY_NO_DISTANCE_FIELD_PATHS'
    ],
  },
}
