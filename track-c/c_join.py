#!/usr/bin/env python3.13

"""Speed Demon 2026, Track C (Join): optimized implementation."""

import argparse
import csv
import os
import re

RECORDS_FILE = "records.csv"
REGIONS_FILE = "regions.csv"
OUTPUT_FILE = "aggregates.txt"

RECORDS_HEADER = ["id", "name", "note", "region_id", "amount"]
REGIONS_HEADER = ["region_id", "country"]

BOM = "\ufeff"
UNMATCHED = "_UNMATCHED"

AMOUNT = re.compile(r"(-?)([0-9]+)\.([0-9]{2})")


def parse_int(text):
    """Convert a valid ASCII integer string into an integer."""
    if not text or not text.isascii() or not text.isdigit():
        raise ValueError(f"bad integer {text!r}")

    return int(text)


def parse_cents(text):
    """Convert an amount into exact integer cents."""
    match = AMOUNT.fullmatch(text)

    if match is None:
        raise ValueError(f"bad amount {text!r}")

    sign, whole, frac = match.groups()

    cents = int(whole) * 100 + int(frac)

    return -cents if sign else cents


def format_cents(cents):
    """Convert integer cents into a decimal amount."""
    sign = "-" if cents < 0 else ""
    cents = abs(cents)

    return f"{sign}{cents // 100}.{cents % 100:02d}"


class Country:
    """Stores aggregate information for one country."""

    __slots__ = (
        "count",
        "id_sum",
        "amount_sum",
        "amount_min",
        "amount_max",
        "names",
        "name_bytes",
        "note_bytes",
    )

    def __init__(self):
        self.count = 0
        self.id_sum = 0
        self.amount_sum = 0
        self.amount_min = None
        self.amount_max = None
        self.names = set()
        self.name_bytes = 0
        self.note_bytes = 0

    def line(self, key):
        return " ".join([
            key,
            str(self.count),
            str(self.id_sum),
            format_cents(self.amount_sum),
            format_cents(self.amount_min),
            format_cents(self.amount_max),
            str(len(self.names)),
            str(self.name_bytes),
            str(self.note_bytes),
        ])


def load_regions(path):
    """Load region IDs and their corresponding countries."""

    regions = {}

    with open(path, encoding="utf-8", newline="") as f:
        reader = csv.reader(f)

        if next(reader) != REGIONS_HEADER:
            raise ValueError("unexpected regions.csv header")

        for region_id, country in reader:
            regions[parse_int(region_id)] = country

    return regions


def read_records(path):
    """Read records.csv while preserving CSV parsing behavior."""

    with open(path, encoding="utf-8", newline="") as f:
        reader = csv.reader(f)

        for number, row in enumerate(reader):

            if len(row) != 5:
                raise ValueError(
                    f"record {number}: expected 5 fields, got {len(row)}"
                )

            # Remove BOM characters at the beginning of records.
            row[0] = row[0].removeprefix(BOM)

            if number == 0:
                if row != RECORDS_HEADER:
                    raise ValueError("unexpected records.csv header")

                continue

            yield row


def main(data_dir, out_dir):

    regions = load_regions(
        os.path.join(data_dir, REGIONS_FILE)
    )

    countries = {}

    get_region = regions.get
    get_country = countries.get
    parse_integer = parse_int
    parse_amount = parse_cents
    unmatched = UNMATCHED

    records_path = os.path.join(data_dir, RECORDS_FILE)

    with open(records_path, encoding="utf-8", newline="") as f:

        reader = csv.reader(f)

        for number, row in enumerate(reader):

            if len(row) != 5:
                raise ValueError(
                    f"record {number}: expected 5 fields, got {len(row)}"
                )

            row[0] = row[0].removeprefix(BOM)

            if number == 0:

                if row != RECORDS_HEADER:
                    raise ValueError("unexpected records.csv header")

                continue

            record_id, name, note, region_id, amount = row

            region_number = parse_integer(region_id)
            record_number = parse_integer(record_id)

            key = get_region(region_number, unmatched)

            country = get_country(key)

            if country is None:
                country = Country()
                countries[key] = country

            cents = parse_amount(amount)

            country.count += 1
            country.id_sum += record_number
            country.amount_sum += cents

            if country.amount_min is None or cents < country.amount_min:
                country.amount_min = cents

            if country.amount_max is None or cents > country.amount_max:
                country.amount_max = cents

            country.names.add(name)

            country.name_bytes += len(name.encode("utf-8"))
            country.note_bytes += len(note.encode("utf-8"))

    lines = [
        countries[key].line(key)
        for key in sorted(countries)
    ]

    os.makedirs(out_dir, exist_ok=True)

    output_path = os.path.join(out_dir, OUTPUT_FILE)

    with open(output_path, "wb") as f:
        f.write(
            "".join(line + "\n" for line in lines).encode("utf-8")
        )

    


def cli(argv=None):

    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[0]
    )

    ap.add_argument("--data", default="/data")
    ap.add_argument("--out", default="/out")

    args = ap.parse_args(argv)

    main(args.data, args.out)

    return 0


if __name__ == "__main__":
    raise SystemExit(cli())