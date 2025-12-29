
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
        libcpp-httplib-dev \
        libevent-dev \
        python3 \
        python3-pip \
        gcovr \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --break-system-packages junit2html

WORKDIR /workspace

CMD ["/bin/bash"]
