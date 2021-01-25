docker rm --force pintos # If it already exists
docker run --name pintos -it -v "$(pwd):/pintos" thierrysans/pintos bash