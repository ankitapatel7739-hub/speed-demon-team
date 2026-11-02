# Build the C++ solution
FROM gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce AS cpp-build

WORKDIR /build

COPY main.cpp .

RUN g++ -O3 -std=c++17 -pthread -o solution main.cpp

# Runtime
FROM python:3.13-slim@sha256:d98cc29d3266e535dd9da1099688d9cb3ca4753813571e071763d17725460962

WORKDIR /app

COPY . /app
COPY --from=cpp-build /build/solution /app/solution

ENTRYPOINT ["python3", "/app/run.py"]