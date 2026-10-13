FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake pkg-config libmariadb-dev libssl-dev ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY backend ./backend
COPY tests/domain_tests.cpp ./tests/domain_tests.cpp
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2 && ctest --test-dir build --output-on-failure
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libmariadb3 libssl3 ca-certificates curl && rm -rf /var/lib/apt/lists/* && useradd -r -u 10001 app
WORKDIR /app
COPY --from=build /src/build/registration /app/registration
COPY frontend /app/frontend
USER app
ENV BIND_HOST=0.0.0.0 FRONTEND_DIR=/app/frontend
EXPOSE 8080
CMD ["/app/registration"]
