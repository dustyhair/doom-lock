FROM ubuntu:24.04
COPY ci/packages.txt /tmp/doom-lock-packages.txt
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    $(cat /tmp/doom-lock-packages.txt) && \
    rm -rf /var/lib/apt/lists/* /tmp/doom-lock-packages.txt
WORKDIR /src
CMD ["sh", "ci/check.sh"]
