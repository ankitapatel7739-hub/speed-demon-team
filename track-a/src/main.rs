use std::collections::HashMap;
use std::env;
use std::fs::{self, File};
use std::io::{BufWriter, Read, Write};

#[derive(Default)]
struct Group {
    latencies: Vec<u32>,
    errors: u32,
}

fn main() {
    let args: Vec<String> = env::args().collect();

    let mut data_dir = String::from("/data");
    let mut out_dir = String::from("/out");

    let mut i = 1;

    while i < args.len() {
        match args[i].as_str() {
            "--data" if i + 1 < args.len() => {
                data_dir = args[i + 1].clone();
                i += 1;
            }
            "--out" if i + 1 < args.len() => {
                out_dir = args[i + 1].clone();
                i += 1;
            }
            _ => {}
        }
        i += 1;
    }

    let input_path = format!("{}/requests.log", data_dir);

    // Read the entire file once.
    let mut data = Vec::new();
    File::open(input_path)
        .unwrap()
        .read_to_end(&mut data)
        .unwrap();

    // Key: (hour, endpoint)
    let mut groups: HashMap<(Vec<u8>, Vec<u8>), Group> = HashMap::new();

    let mut start = 0;

    while start < data.len() {
        let mut end = start;

        while end < data.len() && data[end] != b'\n' {
            end += 1;
        }

        let line = &data[start..end];

        if !line.is_empty() {
            // Hour = YYYY-MM-DDTHH
            let hour = line[0..13].to_vec();

            // Find the spaces:
            // timestamp METHOD endpoint status latency
            let mut spaces = [0usize; 4];
            let mut count = 0;

            for (pos, &c) in line.iter().enumerate() {
                if c == b' ' {
                    spaces[count] = pos;
                    count += 1;

                    if count == 4 {
                        break;
                    }
                }
            }

            let endpoint = line[spaces[1] + 1..spaces[2]].to_vec();

            // Status starts after third space.
            let is_error = line[spaces[2] + 1] == b'5';

            // Parse latency manually.
            let mut latency = 0u32;

            for &c in &line[spaces[3] + 1..] {
                latency = latency * 10 + (c - b'0') as u32;
            }

            let group = groups
                .entry((hour, endpoint))
                .or_default();

            group.latencies.push(latency);

            if is_error {
                group.errors += 1;
            }
        }

        start = end + 1;
    }

    // Convert HashMap to sortable vector.
    let mut entries: Vec<_> = groups.into_iter().collect();

    entries.sort_unstable_by(|a, b| a.0.cmp(&b.0));

    fs::create_dir_all(&out_dir).unwrap();

    let output_path = format!("{}/summary.txt", out_dir);

    let file = File::create(output_path).unwrap();

    let mut writer = BufWriter::with_capacity(1024 * 1024, file);

    for ((hour, endpoint), mut group) in entries {
        group.latencies.sort_unstable();

        let n = group.latencies.len();

        let p50_index = (50 * n + 99) / 100 - 1;
        let p95_index = (95 * n + 99) / 100 - 1;
        let p99_index = (99 * n + 99) / 100 - 1;

        let p50 = group.latencies[p50_index];
        let p95 = group.latencies[p95_index];
        let p99 = group.latencies[p99_index];

        let ppm =
            (group.errors as u64 * 1_000_000 / n as u64) as u32;

        writer.write_all(&hour).unwrap();
        writer.write_all(b":00:00Z ").unwrap();

        writer.write_all(&endpoint).unwrap();
        writer.write_all(b" ").unwrap();

        writeln!(
            writer,
            "{} {} {} {} {} {} {}",
            n,
            p50,
            p95,
            p99,
            group.errors,
            ppm
        )
        .unwrap();
    }
}