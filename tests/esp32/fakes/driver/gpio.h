#ifndef PET_TEST_GPIO_H
#define PET_TEST_GPIO_H
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) <= 46)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) >= 0 && (pin) < 46)
#endif
