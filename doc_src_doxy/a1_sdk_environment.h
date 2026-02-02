
#ifndef _SDK_ENVIRONMENT_H_
#define _SDK_ENVIRONMENT_H_

/**
@page sdk_environment SDK Environment setup

As a prerequisite for this guide, you must be able to successfully build
one of the provided application example from the SDK.

This page contains following sections:
- @subpage installation_of_sdk_environment
- @subpage flashing_guideline
- @subpage nordic_resources
- @subpage nordic_resources_nRF52_and_nRF54L
- @subpage efr32_resources

@section installation_of_sdk_environment Installation of SDK Environment

To ease the management of SDK environement, Wirepas maintains a docker image with
all the required dependencies installed.

For more information to use it, please read guidance from <a href="https://developer.wirepas.com/support/solutions/articles/77000435375">
Wirepas Helpdesk.</a>

It is also possible to install requirement in your native environement but it is not described here.
Requirement are listed in Github SDK main page under Requirement section.

@section flashing_guideline Flashing devices

Checkout flashing guidance from <a href="https://developer.wirepas.com/support/solutions/articles/77000465762">
Wirepas Helpdesk.</a>


@section nordic_resources_nRF52_and_nRF54L Resources on Nordic nRF52 and nRF54L

The nRF52 chip version supported by Wirepas Mesh has minimum 512 kB of NVM (flash) and
64 kB of RAM.

The nRF54L chip version supported by Wirepas Mesh has minimum 500 kB of NVM (RRAM) and
96 kB of RAM.

This page contains following sections:
- @subpage nvm_memory_nrf52_and_nrf54l
- @subpage ram_memory_nrf52_and_nrf54l
- @subpage peripherals_accessible_by_stack_only_nrf52_and_nrf54l
- @subpage peripherals_shared_between_the_stack_and_the_application_nrf52_and_nrf54l
- @subpage peripherals_available_for_the_application_nrf52_and_nrf54l

@subsection nvm_memory_nrf52_and_nrf54l NVM available for application on nRF52 and nRF54L

As stated in [description of memory partitioning](@ref memory_partitioning), the
available non-volatile memory for application is limited by size of the memory area
that is used commonly for application and also scratchpad image. If application
size is too large, there is possibility that large scratchpad image will
override application image. The default maximum size of the application has been
set so that it is always safe to use scratchpad image that will contain both
firmware and application.

The _recommended_ maximum size of [NVM](@ref flash_memory) for an
application, according to processor type is following:

<table>
<tr><th>Processor</th><th>NVM</th></tr>
<tr><td>nRF52832</td><td>50 kB, Flash</td></tr>
<tr><td>nRF52833</td><td>50 kB, Flash</td></tr>
<tr><td>nRF52840</td><td>256 kB, Flash</td></tr>
<tr><td>nRF54L05</td><td>256 kB, RRAM</td></tr>
<tr><td>nRF54L10</td><td>256 kB, RRAM</td></tr>
<tr><td>nRF54L15</td><td>256 kB, RRAM</td></tr>
</table>

@subsection ram_memory_nrf52_and_nrf54l RAM available for application on nRF52 and nRF54L

Allocated [RAM](@ref ram_memory) for application by the processor is
following:
<table>
<tr><th>Processor</th><th>RAM</th></tr>
<tr><td>nRF52832</td><td>Up to 16 kB (8 kB by default)</td></tr>
<tr><td>nRF52833</td><td>72 kB</td></tr>
<tr><td>nRF52840</td><td>188 kB</td></tr>
<tr><td>nRF54L05</td><td>48 kB</td></tr>
<tr><td>nRF54L10</td><td>96 kB</td></tr>
<tr><td>nRF54L15</td><td>128 kB</td></tr>
</table>

@subsection peripherals_accessible_by_stack_only_nrf52_and_nrf54l Peripherals accessible by stack only

Some peripherals are used by the Wirepas Mesh stack and cannot be
used by the application.

<table>
<tr><th>Peripheral</th><th>Associated interrupt nRF52</th><th>Associated interrupt nRF54L</th></tr>
<tr><td>Power</td><td><code>POWER_CLOCK_IRQn</code></td><td><code>CLOCK_POWER_IRQn</code></td></tr>
<tr><td>Radio</td><td><code>RADIO_IRQn</code></td><td><code>RADIO_0_IRQn</code></td></tr>
<tr><td>Timer</td><td><code>TIMER0_IRQn</code></td><td><code>TIMER24_IRQn</code></td></tr>
<tr><td>WDT</td><td><code>WDT_IRQn</code></td><td><code>WDT30_IRQn</code></td></tr>
<tr><td>RTC</td><td><code>RTC1_IRQn</code></td><td><code>GRTC_0_IRQn</code></td></tr>
<tr><td>ECB (AES)</td><td><code>ECB_IRQn</code></td><td><code>ECB00_IRQn</code></td></tr>
<tr><td>PPI (Channels 0, 1 and 2)</td><td>None</td><td>None</td></tr>
</table>

All the internal interrupt of cortex M are handled by the stack
directly (NMI, HardFault,...)

@subsection peripherals_shared_between_the_stack_and_the_application_nrf52_and_nrf54l Peripherals shared between the stack and the application

Some peripherals are used by the stack but can also be accessed by the
application.

Random Number Generator RNG is available for application to use within App_init function.
After App_init returns, this peripheral is reserved for Wirepas Mesh stack and
all initializations done in App_init may be overwritten.
Application may also take the control of RNG/TRNG by initializing the peripheral in
scheduled task after App_init has returned and after Wirepas Mesh stack has started.
Do note that initialization must not take place within interrupt context as interrupt
could be served before these peripherals are released from Wirepas Mesh stack usage.

@subsection peripherals_available_for_the_application_nrf52_and_nrf54l Peripherals available for the application

All the other peripherals not listed above are free to be used by the application.



@section efr32_resources Resources on EFR32

Following chip variants (at 2.4 GHz only) are supported:

-   EFR32FG12P232F1024G L125/M48    [1024/128, BGA125/QFN48]
-   EFR32FG12P432F1024G L125/M48    [1024/256, BGA125/QFN48]
-   EFR32FG12P433F1024G L125/M48    [1024/256, BGA125/QFN48]
-   EFR32MG12P232F1024G L125/M48    [1024/128, BGA125/QFN48]
-   EFR32MG12P332F1024G L125/M48    [1024/128, BGA125/QFN48]
-   EFR32MG12P432F1024G L125/M48    [1024/256, BGA125/QFN48]
-   EFR32MG12P433F1024G L125/M48    [1024/256, BGA125/QFN48]
-   EFR32BG12P232F1024G L125/M48    [1024/128, BGA125/QFN48]
-   EFR32BG12P432F1024G L125/M48    [1024/256, BGA125/QFN48]
-   EFR32BG12P433F1024G L125/M48    [1024/256, BGA125/QFN48]
-   EFR32BG21A010F1024IM32
-   EFR32BG21A010F512IM32
-   EFR32BG21A010F768IM32
-   EFR32BG21A020F1024IM32
-   EFR32BG21A020F512IM32
-   EFR32BG21A020F768IM32
-   EFR32BG22C224F512GM32
-   EFR32BG22C224F512GM40
-   EFR32BG22C224F512GN32
-   EFR32BG22C224F512IM32
-   EFR32BG22C224F512IM40
-   BGM220PC22HNA
-   BGM220SC22HNA
-   EFR32MG24B310F1536IM48-B
-   EFR32MG24B210F1536IM48-B
-   EFR32MG24B310F1536IM48-B [10 dBm 1536/256 SV=High QFN48]
-   EFR32MG24B220F1536IM48-B [19.5 dBm 1536/256 SV=High QFN48]
-   EFR32MG24B210F1536IM48-B [10 dBm 1536/256 SV=High QFN48]
-   EFR32MG24B120F1536IM48-B [19.5 dBm 1536/256 SV=High QFN48]
-   EFR32MG24B110F1536IM48-B [10 dBm 1536/256 SV=High QFN48]
-   EFR32MG24B020F1536IM48-B [19.5 dBm 1536/256 SV=High QFN48]
-   EFR32MG24B020F1536IM40-B [19.5 dBm 1536/256 SV=High QFN40]
-   EFR32MG24B020F1024IM48-B [19.5 dBm 1024/128 SV=High QFN48]
-   EFR32MG24B010F1536IM48-B [10 dBm 1536/256 SV=High QFN48]
-   EFR32MG24B010F1536IM40-B [10 dBm 1536/256 SV=High QFN40]
-   EFR32MG24B010F1024IM48-B [10 dBm 1024/128 SV=High QFN48]
-   EFR32MG24A420F1536IM48-B [19.5 dBm 1536/256 SV=Mid QFN48]
-   EFR32MG24A420F1536IM40-B [19.5 dBm 1536/256 SV=Mid QFN40]
-   EFR32MG24A410F1536IM48-B [10 dBm 1536/256 SV=Mid QFN48]
-   EFR32MG24A410F1536IM40-B [10 dBm 1536/256 SV=Mid QFN40]
-   EFR32MG24A110F1024IM48-B [10 dBm 1024/128 SV=Mid QFN48]
-   EFR32MG24A021F1024IM40-B [19.5 dBm 1024/128 SV=Mid QFN40]
-   EFR32MG24A020F1536IM48-B [19.5 dBm 1536/192 SV=Mid QFN48]
-   EFR32MG24A020F1536IM40-B [19.5 dBm 1536/192 SV=Mid QFN40]
-   EFR32MG24A020F1024IM48-B [19.5 dBm 1024/128 SV=Mid QFN48]
-   EFR32MG24A020F1024IM40-B [19.5 dBm 1024/128 SV=Mid QFN40]
-   EFR32MG24A010F1536IM48-B [10 dBm 1536/192 SV=Mid QFN48]
-   EFR32MG24A010F1536IM40-B [10 dBm 1536/192 SV=Mid QFN40]
-   EFR32MG24A010F1024IM48-B [10 dBm 1024/128 SV=Mid QFN48]
-   EFR32MG24A010F1024IM40-B [10 dBm 1024/128 SV=Mid QFN40]


This page contains following sections:
- @subpage flash_memory_efr32
- @subpage ram_memory_efr32
- @subpage peripherals_accessible_by_stack_only_efr32
- @subpage peripherals_shared_between_the_stack_and_the_application_efr32
- @subpage peripherals_available_for_the_application_efr32

@subsection flash_memory_efr32 Flash Memory available for application on EFR32

As stated in [description of memory partitioning](@ref memory_partitioning), the
available flash memory for application is limited by size of the memory area
that is used commonly for application and also scratchpad image. If application
size is too large, there is possibility that large scratchpad image will
override application image. The default maximum size of the application has been
set so that it is always safe to use scratchpad image that will contain both
firmware and application.

The _recommended_ maximum size of [flash memory](@ref flash_memory) for an
application, according to processor type is following:

<table>
<tr><th>Processor</th><th>Flash memory</th></tr>
<tr><td>efr32xg12pxxxf1024</td><td>256kB</td></tr>
<tr><td>efr32xg12pxxxf512</td><td>50kB</td></tr>
<tr><td>efr32xg21xxxxf512</td><td>50kB</td></tr>
<tr><td>efr32xg21xxxxf768</td><td>50kB</td></tr>
<tr><td>efr32xg21xxxxf1024</td><td>256kB</td></tr>
<tr><td>efr32xg22xxxxf512</td><td>50kB</td></tr>
<tr><td>efr32xg24xxxxf1024</td><td>256kB</td></tr>
<tr><td>efr32xg24xxxxf1536_196</td><td>256kB</td></tr>
<tr><td>efr32xg24xxxxf1536_256</td><td>256kB</td></tr>
</table>


@subsection ram_memory_efr32 RAM Memory available for application on EFR32

Allocated [RAM memory](@ref ram_memory) for application, by the processor is
following:

<table>
<tr><th>Processor</th><th>RAM memory</th></tr>
<tr><td>efr32xg12pxxxf1024</td><td>72kB</td></tr>
<tr><td>efr32xg12pxxxf512</td><td>8kB</td></tr>
<tr><td>efr32xg21xxxxf512</td><td>12kB</td></tr>
<tr><td>efr32xg21xxxxf768</td><td>12kB</td></tr>
<tr><td>efr32xg21xxxxf1024</td><td>44kB</td></tr>
<tr><td>efr32xg22xxxxf512</td><td>4.5kB</td></tr>
<tr><td>efr32xg24xxxxf1024</td><td>72kB</td></tr>
<tr><td>efr32xg24xxxxf1536_196</td><td>140kB</td></tr>
<tr><td>efr32xg24xxxxf1536_256</td><td>155kB</td></tr>
</table>


@subsection peripherals_accessible_by_stack_only_efr32 Peripherals accessible by stack only

Some peripherals are used by the Wirepas Mesh stack and cannot be used by the application.

<table>
<tr><th>Peripheral</th><th>Associated interrupt (from chip vendor files)</th></tr>
<tr><td><code>TIMER0</code></td><td><code>TIMER0_IRQn</code></td></tr>
<tr><td><code>RTCC</code></td><td><code>RTCC_IRQn</code></td></tr>
<tr><td><code>WDOG0</code></td><td><code>WDOG0_IRQn</code></td></tr>
<tr><td><code>CMU</code></td><td><code>CMU_IRQn</code></td></tr>
<tr><td><code>CRYPTO0</code></td><td><code>CRYPTO0_IRQn</code></td></tr>
</table>

All the internal interrupt of cortex M are handled by the stack directly (NMI, HardFault,...)

@subsection peripherals_shared_between_the_stack_and_the_application_efr32 Peripherals shared between the stack and the application

True Random Number Generator TRNG is available for application to use within App_init function.
After App_init returns, this peripheral is reserved for Wirepas Mesh stack and all initializations done in App_init may be overwritten.
Application may also take the control of RNG/TRNG by initializing the peripheral in scheduled task after App_init has returned and after
Wirepas Mesh stack has started. Do note that initialization must not take place within interrupt context as interrupt could be served
before this peripheral is released from Wirepas Mesh stack usage.

@subsection peripherals_available_for_the_application_efr32 Peripherals available for the application

All the other peripherals not listed above are free to be used by the application.

Related Material
================

@anchor relmat3 [3] WP-RM-108 - OTAP Reference Manual

@anchor relmat4 [4] https://github.com/wirepas/wm-sdk/blob/master/source/reference_apps/dualmcu_app/api/DualMcuAPI.md

*/


#endif /* API_DOC_SDK_ENVIRONMENT_H_ */
