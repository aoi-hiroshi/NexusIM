FROM nginx:1.31.7

RUN nginx -V 2>&1 | grep -q -- '--with-stream=dynamic' && \
    test -f /usr/lib/nginx/modules/ngx_stream_module.so

COPY deploy/docker/nginx/nginx.conf /etc/nginx/nginx.conf
