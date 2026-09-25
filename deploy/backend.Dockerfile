FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git cmake g++ libjsoncpp-dev uuid-dev zlib1g-dev libssl-dev libpng-dev nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch v1.9.13 --recurse-submodules --shallow-submodules https://github.com/drogonframework/drogon.git /tmp/drogon \
    && cmake -S /tmp/drogon -B /tmp/drogon-build -DCMAKE_BUILD_TYPE=Release -DBUILD_CTL=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_ORM=OFF \
    && cmake --build /tmp/drogon-build -j2 && cmake --install /tmp/drogon-build && ldconfig
RUN git clone --depth 1 --branch v0.7.1 https://github.com/Thalhammer/jwt-cpp.git /tmp/jwt-cpp \
    && cp -r /tmp/jwt-cpp/include/jwt-cpp /tmp/jwt-cpp/include/picojson /usr/local/include/
WORKDIR /app
COPY CMakeLists.txt ./
COPY src ./src
COPY tests/backend ./tests/backend
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j2 && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libjsoncpp25 libuuid1 zlib1g libssl3t64 libpng16-16t64 libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home store \
    && mkdir -p /app /var/lib/store/uploads && chown -R store:store /var/lib/store
COPY --from=build /usr/local/lib/ /usr/local/lib/
COPY --from=build /app/build/phase1_server /usr/local/bin/phase1_server
RUN ldconfig
WORKDIR /app
USER store
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD curl --fail --silent http://127.0.0.1:8080/health > /dev/null || exit 1
CMD ["phase1_server"]
