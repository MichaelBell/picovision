#include <stdint.h>
#include "hardware/pio.h"

namespace pimoroni {
    class USBGPIO {
        public:
            USBGPIO(PIO pio = pio0);

            bool init();

            uint32_t get_state() { return state; }

        private:
            PIO pio;
            uint16_t pio_sm;
            uint16_t pio_offset;

            uint8_t dma_channel_to_pio;
            uint8_t dma_channel_from_pio;
            uint8_t dma_timer;
            bool is_initialized = false;

            volatile uint32_t state;
    };
}
