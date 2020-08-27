#!/usr/bin/env bash

cat > /etc/apt/sources.list <<EOF
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ focal main restricted
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ focal-updates main restricted
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ focal universe
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ focal-updates universe
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ focal multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ focal-updates multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ focal-backports main restricted universe multiverse
deb [arch=amd64] http://security.ubuntu.com/ubuntu/ focal-security main restricted
deb [arch=amd64] http://security.ubuntu.com/ubuntu/ focal-security universe
deb [arch=amd64] http://security.ubuntu.com/ubuntu/ focal-security multiverse
deb [arch=arm64] http://ports.ubuntu.com/ focal main restricted
deb [arch=arm64] http://ports.ubuntu.com/ focal-updates main restricted
deb [arch=arm64] http://ports.ubuntu.com/ focal universe
deb [arch=arm64] http://ports.ubuntu.com/ focal-updates universe
deb [arch=arm64] http://ports.ubuntu.com/ focal multiverse
deb [arch=arm64] http://ports.ubuntu.com/ focal-updates multiverse
deb [arch=arm64] http://ports.ubuntu.com/ focal-backports main restricted universe multiverse
EOF
