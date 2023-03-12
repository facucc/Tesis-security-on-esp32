#!/bin/bash

docker-compose build esp-idf
docker-compose run esp-idf idf.py build


