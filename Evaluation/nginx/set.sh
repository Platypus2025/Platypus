mkdir server
cd server

mkdir -p html logs client_body_temp fastcgi_temp proxy_temp scgi_temp uwsgi_temp temp

truncate -s 20480 ./html/test20k

cd ..