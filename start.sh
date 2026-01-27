#!/bin/bash

cd ./web
bun dev &

cd ..
cd ./mqtt-server/python-serial-link

source ./venv/bin/activate
python ./main.py

exit 0