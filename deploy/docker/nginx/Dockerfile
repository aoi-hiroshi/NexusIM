FROM nginx:1.31.7

RUN nginx -V 2>&1 | grep -q -- '--with-stream'

COPY deploy/docker/nginx/nginx.conf /etc/nginx/nginx.conf
