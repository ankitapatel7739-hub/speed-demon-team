use std::cmp::Reverse;
use std::collections::BinaryHeap;
use std::env;
use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Read, Write};

struct FastInput {
    data: Vec<u8>,
    pos: usize,
}

impl FastInput {
    fn new(path: &str) -> Self {
        let mut data = Vec::new();
        BufReader::new(File::open(path).unwrap())
            .read_to_end(&mut data)
            .unwrap();

        Self { data, pos: 0 }
    }

    #[inline]
    fn next_usize(&mut self) -> usize {
        while self.pos < self.data.len()
            && (self.data[self.pos] < b'0' || self.data[self.pos] > b'9')
        {
            self.pos += 1;
        }

        let mut value = 0usize;

        while self.pos < self.data.len()
            && self.data[self.pos] >= b'0'
            && self.data[self.pos] <= b'9'
        {
            value = value * 10 + (self.data[self.pos] - b'0') as usize;
            self.pos += 1;
        }

        value
    }
}

struct Graph {
    n: usize,

    out_start: Vec<usize>,
    out_to: Vec<usize>,
    out_w: Vec<u64>,

    in_start: Vec<usize>,
    in_to: Vec<usize>,
    in_w: Vec<u64>,
}

fn read_graph(path: &str) -> Graph {
    let mut input = FastInput::new(path);

    let n = input.next_usize();
    let m = input.next_usize();

    let mut us = Vec::with_capacity(m);
    let mut vs = Vec::with_capacity(m);
    let mut ws = Vec::with_capacity(m);

    let mut out_count = vec![0usize; n];
    let mut in_count = vec![0usize; n];

    for _ in 0..m {
        let u = input.next_usize();
        let v = input.next_usize();
        let w = input.next_usize() as u64;

        us.push(u);
        vs.push(v);
        ws.push(w);

        out_count[u] += 1;
        in_count[v] += 1;
    }

    let mut out_start = vec![0usize; n + 1];
    let mut in_start = vec![0usize; n + 1];

    for i in 0..n {
        out_start[i + 1] = out_start[i] + out_count[i];
        in_start[i + 1] = in_start[i] + in_count[i];
    }

    let mut out_to = vec![0usize; m];
    let mut out_w = vec![0u64; m];

    let mut in_to = vec![0usize; m];
    let mut in_w = vec![0u64; m];

    let mut out_pos = out_start[..n].to_vec();
    let mut in_pos = in_start[..n].to_vec();

    for i in 0..m {
        let u = us[i];
        let v = vs[i];
        let w = ws[i];

        let p = out_pos[u];
        out_to[p] = v;
        out_w[p] = w;
        out_pos[u] += 1;

        let p = in_pos[v];
        in_to[p] = u;
        in_w[p] = w;
        in_pos[v] += 1;
    }

    Graph {
        n,
        out_start,
        out_to,
        out_w,
        in_start,
        in_to,
        in_w,
    }
}

struct Solver {
    graph: Graph,

    dist_f: Vec<u64>,
    dist_b: Vec<u64>,

    mark_f: Vec<u32>,
    mark_b: Vec<u32>,

    query_id: u32,
}

impl Solver {
    fn new(graph: Graph) -> Self {
        let n = graph.n;

        Self {
            graph,
            dist_f: vec![0; n],
            dist_b: vec![0; n],
            mark_f: vec![0; n],
            mark_b: vec![0; n],
            query_id: 0,
        }
    }

    #[inline]
    fn shortest_path(&mut self, source: usize, target: usize) -> i64 {
        if source == target {
            return 0;
        }

        if self.graph.out_start[source] == self.graph.out_start[source + 1] {
            return -1;
        }

        if self.graph.in_start[target] == self.graph.in_start[target + 1] {
            return -1;
        }

        self.query_id += 1;
        let qid = self.query_id;

        let mut heap_f: BinaryHeap<Reverse<(u64, usize)>> = BinaryHeap::new();
        let mut heap_b: BinaryHeap<Reverse<(u64, usize)>> = BinaryHeap::new();

        self.dist_f[source] = 0;
        self.mark_f[source] = qid;
        heap_f.push(Reverse((0, source)));

        self.dist_b[target] = 0;
        self.mark_b[target] = qid;
        heap_b.push(Reverse((0, target)));

        let mut best = u64::MAX;

        loop {
            while let Some(&Reverse((d, u))) = heap_f.peek() {
                if self.mark_f[u] == qid && self.dist_f[u] == d {
                    break;
                }
                heap_f.pop();
            }

            while let Some(&Reverse((d, u))) = heap_b.peek() {
                if self.mark_b[u] == qid && self.dist_b[u] == d {
                    break;
                }
                heap_b.pop();
            }

            if heap_f.is_empty() || heap_b.is_empty() {
                break;
            }

            let min_f = heap_f.peek().unwrap().0.0;
            let min_b = heap_b.peek().unwrap().0.0;

            if min_f.saturating_add(min_b) >= best {
                break;
            }

            if min_f <= min_b {
                let Reverse((d, u)) = heap_f.pop().unwrap();

                if self.mark_b[u] == qid {
                    best = best.min(d + self.dist_b[u]);
                }

                let start = self.graph.out_start[u];
                let end = self.graph.out_start[u + 1];

                for i in start..end {
                    let v = self.graph.out_to[i];
                    let nd = d + self.graph.out_w[i];

                    if self.mark_f[v] != qid || nd < self.dist_f[v] {
                        self.dist_f[v] = nd;
                        self.mark_f[v] = qid;
                        heap_f.push(Reverse((nd, v)));

                        if self.mark_b[v] == qid {
                            best = best.min(nd + self.dist_b[v]);
                        }
                    }
                }
            } else {
                let Reverse((d, u)) = heap_b.pop().unwrap();

                if self.mark_f[u] == qid {
                    best = best.min(d + self.dist_f[u]);
                }

                let start = self.graph.in_start[u];
                let end = self.graph.in_start[u + 1];

                for i in start..end {
                    let v = self.graph.in_to[i];
                    let nd = d + self.graph.in_w[i];

                    if self.mark_b[v] != qid || nd < self.dist_b[v] {
                        self.dist_b[v] = nd;
                        self.mark_b[v] = qid;
                        heap_b.push(Reverse((nd, v)));

                        if self.mark_f[v] == qid {
                            best = best.min(nd + self.dist_f[v]);
                        }
                    }
                }
            }
        }

        if best == u64::MAX {
            -1
        } else {
            best as i64
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();

    let mut data_dir = String::from("/data");
    let mut out_dir = String::from("/out");

    let mut i = 1;

    while i < args.len() {
        match args[i].as_str() {
            "--data" => {
                if i + 1 < args.len() {
                    data_dir = args[i + 1].clone();
                    i += 1;
                }
            }
            "--out" => {
                if i + 1 < args.len() {
                    out_dir = args[i + 1].clone();
                    i += 1;
                }
            }
            _ => {}
        }

        i += 1;
    }

    let graph_path = format!("{}/graph.txt", data_dir);
    let queries_path = format!("{}/queries.txt", data_dir);

    let graph = read_graph(&graph_path);
    let mut solver = Solver::new(graph);

    let mut queries_input = FastInput::new(&queries_path);

    let mut queries: Vec<(usize, usize)> = Vec::new();

    while queries_input.pos < queries_input.data.len() {
        while queries_input.pos < queries_input.data.len()
            && (queries_input.data[queries_input.pos] < b'0'
                || queries_input.data[queries_input.pos] > b'9')
        {
            queries_input.pos += 1;
        }

        if queries_input.pos >= queries_input.data.len() {
            break;
        }

        let source = queries_input.next_usize();

        if queries_input.pos >= queries_input.data.len() {
            break;
        }

        let target = queries_input.next_usize();

        queries.push((source, target));
    }

    let mut answers = Vec::with_capacity(queries.len());

    for (source, target) in queries {
        answers.push(solver.shortest_path(source, target));
    }

    fs::create_dir_all(&out_dir).unwrap();

    let output_path = format!("{}/distances.txt", out_dir);

    let file = File::create(output_path).unwrap();
    let mut writer = BufWriter::with_capacity(1024 * 1024, file);

    for answer in answers {
        writeln!(writer, "{}", answer).unwrap();
    }
}