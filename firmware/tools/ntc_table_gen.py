#!/usr/bin/env python3
"""Generate the NTC thermistor ADC lookup table used by src/adc.c."""

import argparse
import math


ADC_FULL_SCALE = 1023
ROOM_TEMPERATURE_C = 25.0
KELVIN_OFFSET = 273.15


def positive_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be a finite number greater than zero")
    return parsed


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def adc_value(
    temperature_c: float,
    room_resistance_kohm: float,
    beta: float,
    pullup_kohm: float,
) -> int:
    temperature_k = temperature_c + KELVIN_OFFSET
    room_temperature_k = ROOM_TEMPERATURE_C + KELVIN_OFFSET
    resistance_kohm = room_resistance_kohm * math.exp(
        beta * ((1.0 / temperature_k) - (1.0 / room_temperature_k))
    )
    adc = ADC_FULL_SCALE * resistance_kohm / (resistance_kohm + pullup_kohm)
    return math.floor(adc + 0.5)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a 10-bit ADC lookup table for an NTC thermistor."
    )
    parser.add_argument(
        "--room-resistance-kohm",
        type=positive_float,
        default=10.0,
        help="NTC resistance at 25 C in kilo-ohms (default: 10)",
    )
    parser.add_argument(
        "--beta",
        type=positive_float,
        default=3950.0,
        help="NTC beta value in kelvin (default: 3950)",
    )
    parser.add_argument(
        "--pullup-kohm",
        type=positive_float,
        default=2.2,
        help="pull-up resistance in kilo-ohms (default: 2.2)",
    )
    parser.add_argument(
        "--step-c",
        type=positive_float,
        default=10.0,
        help="temperature step in degrees Celsius (default: 10)",
    )
    parser.add_argument(
        "--table-size",
        type=positive_int,
        default=15,
        help="number of steps after the special index 0 entry (default: 15)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    print("static const uint16_t ntc_adc_by_10c[] = {")
    # Index 0 is the 0 C endpoint; table_size counts the entries after it.
    for index in range(args.table_size + 1):
        value = adc_value(
            index * args.step_c,
            args.room_resistance_kohm,
            args.beta,
            args.pullup_kohm,
        )
        print(f"    {value},")
    print("};")


if __name__ == "__main__":
    main()
