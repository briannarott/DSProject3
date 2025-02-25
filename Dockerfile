FROM ubuntu:22.04

RUN apt-get update
RUN apt-get install -y gcc

WORKDIR /app/

ADD membership.c /app/
ADD membership.h /app/
ADD hostsfile.txt /app/

RUN gcc membership.c -o membership -pthread

ENTRYPOINT ["/app/membership"]

