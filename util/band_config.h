/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

   else 
   {
       return APP_RES_INVALID_VALUE;
   }
   return lib_radio_cfg->bandSetup(band_mask);
#else
   (void)band_group;
   return APP_RES_NOT_IMPLEMENTED;
#endif
}

#endif

