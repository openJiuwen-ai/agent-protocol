
FROM gcc:13

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        cmake \
        make \
        gcc \
        g++ \
        git \
        libcurl4 \
        libcurl4-openssl-dev \
        nlohmann-json3-dev \
        libevent-dev \
        python3 \
        python3-pip \
        gcovr \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --break-system-packages junit2html

# 下载并安装 http_parser v2.9.4
RUN curl -L https://github.com/nodejs/http-parser/archive/refs/tags/v2.9.4.tar.gz \
    -o /tmp/http_parser.tar.gz \
    && tar -xzf /tmp/http_parser.tar.gz -C /tmp \
    && cd /tmp/http-parser-2.9.4 \
    && make package \
    && make install \
    && ldconfig \
    && rm -rf /tmp/*

# 下载并安装 GoogleTest v1.16.0
RUN curl -L https://github.com/google/googletest/archive/refs/tags/v1.16.0.tar.gz \
    -o /tmp/gtest.tar.gz \
    && tar -xzf /tmp/gtest.tar.gz -C /tmp \
    && cd /tmp/googletest-1.16.0 \
    && cmake -B build -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build build --target install -j$(nproc) \
    && rm -rf /tmp/*

# 下载 cpp-httplib v0.18.7
RUN curl -L https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.7.tar.gz \
    -o /tmp/httplib.tar.gz \
    && tar -xzf /tmp/httplib.tar.gz -C /tmp \
    && cp /tmp/cpp-httplib-0.18.7/httplib.h /usr/local/include/ \
    && rm -rf /tmp/*

WORKDIR /workspace

CMD ["/bin/bash"]
