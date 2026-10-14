FROM gcc:latest
COPY . /app
WORKDIR /app
RUN g++ -std=c++17 web_warciboys.cpp -o server -lpthread
EXPOSE 8080
CMD ["./server"]