FROM gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce AS builder

WORKDIR /app
COPY b_index.cpp .
RUN g++ -O2 -std=c++17 -pthread b_index.cpp -o b_index

FROM python:3.13-slim@sha256:d98cc29d3266e535dd9da1099688d9cb3ca4753813571e071763d17725460962

WORKDIR /app
COPY . /app
COPY --from=builder /app/b_index /app/b_index

RUN chmod +x /app/b_index
ENTRYPOINT ["/app/b_index", "--data", "/data", "--out", "/out"]