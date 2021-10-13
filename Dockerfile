FROM pgrouting/pgrouting:12-3.0-master
# FROM pgrouting/pgrouting:latest  // こっちでは動かないみたい(予想)

RUN apt update 
RUN apt install -y libpqxx-6.2
RUN apt install -y build-essential cmake wget \ 
       libboost-program-options-dev libexpat1 \
        libexpat-dev libosmium2-dev libpqxx-dev \
        zlib1g-dev
RUN cd /usr/local/src
RUN wget https://github.com/pgRouting/osm2pgrouting/archive/v2.3.6.tar.gz
RUN tar xvf v2.3.6.tar.gz
RUN cd osm2pgrouting-2.3.6 \
 && mkdir build \
 && cd build \ 
 && cmake .. \
 && make \
 && make install \
 && cd ../tools/osmium/ \
 && mkdir build \
 && cd build \
 && cmake .. \
 && make \
 && make install \
 && cd /usr/local/src \
 && rm -rf ./* 
 RUN apt purge -y --autoremove \
        build-essential \
        cmake \
        wget \
        libexpat-dev \
        libosmium2-dev \
        libpqxx-dev \
        zlib1g-dev 
 RUN apt autoremove -y 
 RUN rm -rf /var/lib/apt/lists/*