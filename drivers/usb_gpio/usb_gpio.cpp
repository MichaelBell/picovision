#include "usb_gpio.hpp"
#include "hardware/dma.h"
#include "hardware/structs/usb.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "usb_gpio.pio.h"

using namespace pimoroni;

USBGPIO::USBGPIO(PIO pio) : pio(pio)
{}

bool USBGPIO::init() {
    // Check that the USB is not connected (e.g. to a PC running Thonny) before messing with it
    // First make sure enough time has elapsed for a connection to be made
    absolute_time_t minimum_time = from_us_since_boot(500000);
    absolute_time_t current_time = get_absolute_time();
    if (absolute_time_diff_us(minimum_time, current_time) < 0) {
        sleep_until(minimum_time);
    }

    if ((usb_hw->sie_status & USB_SIE_STATUS_CONNECTED_BITS) != 0) {
        // USB is connected - do nothing
        return false;
    }

    if (is_initialized) {
        return true;
    }
    is_initialized = true;

    // Override USB PHY control
    usb_hw->phy_direct_override = USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_EN_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_DM_PULLDN_EN_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_DP_PULLDN_EN_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS;

    // Pull up both DP and DM, this is an invalid USB state.
    // The PicoSystem detects this to know it is connected to a PicoVision
    usb_hw->phy_direct = USB_USBPHY_DIRECT_DM_PULLUP_EN_BITS | USB_USBPHY_DIRECT_DP_PULLUP_EN_BITS;

    pio_sm = pio_claim_unused_sm(pio, true);
    pio_offset = pio_add_program(pio, &usb_gpio_program);
    usb_gpio_program_init(pio, pio_sm, pio_offset);

    dma_channel_from_pio = dma_claim_unused_channel(true);
    dma_channel_to_pio = dma_claim_unused_channel(true);
    dma_timer = dma_claim_unused_timer(true);

    const uint divider = clock_get_hz(clk_sys) / 500000;
    dma_timer_set_fraction(dma_timer, 1, divider);

    dma_channel_config c = dma_channel_get_default_config(dma_channel_to_pio);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, dma_get_timer_dreq(dma_timer));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    
    dma_channel_configure(
        dma_channel_to_pio, &c,
        &pio->txf[pio_sm],
//        &state,
        &usb_hw->phy_direct,
        0xFFFFFFFF,
        true
    );

    c = dma_channel_get_default_config(dma_channel_from_pio);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, pio_sm, false));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    
    dma_channel_configure(
        dma_channel_from_pio, &c,
        &state,
        &pio->rxf[pio_sm],
        0xFFFFFFFF,
        true
    );

    return true;
}