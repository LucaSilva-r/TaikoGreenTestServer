FROM ubuntu:20.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates wget gnupg apt-transport-https \
    && wget -O /tmp/packages-microsoft-prod.deb https://packages.microsoft.com/config/ubuntu/20.04/packages-microsoft-prod.deb \
    && dpkg -i /tmp/packages-microsoft-prod.deb \
    && apt-get update \
    && apt-get install -y --no-install-recommends dotnet-sdk-8.0 \
    && rm -rf /var/lib/apt/lists/* /tmp/packages-microsoft-prod.deb

WORKDIR /src
COPY . .
RUN dotnet publish TaikoGreenTestServer/TaikoGreenTestServer.csproj -c Release -o /app

FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive \
    ASPNETCORE_ENVIRONMENT=Production \
    DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates wget gnupg apt-transport-https openssl build-essential libgnutls28-dev \
    && wget -O /tmp/packages-microsoft-prod.deb https://packages.microsoft.com/config/ubuntu/20.04/packages-microsoft-prod.deb \
    && dpkg -i /tmp/packages-microsoft-prod.deb \
    && apt-get update \
    && apt-get install -y --no-install-recommends aspnetcore-runtime-8.0 \
    && rm -rf /var/lib/apt/lists/* /tmp/packages-microsoft-prod.deb

COPY docker/openssl.cnf /etc/ssl/openssl.cnf
COPY docker/legacy_tls_proxy.c /tmp/legacy_tls_proxy.c
RUN gcc -O2 -Wall -Wextra -o /usr/local/bin/legacy-tls-proxy /tmp/legacy_tls_proxy.c -lgnutls -lpthread \
    && rm /tmp/legacy_tls_proxy.c
COPY --from=build /app /app
COPY docker/appsettings.Container.json /app/appsettings.json
RUN openssl pkcs12 -in /app/Certificates/cert.pfx -nodes -passin pass: -out /tmp/taiko-cert.pem \
    && awk 'BEGIN{c=0} /BEGIN CERTIFICATE/{c=1} c{print} /END CERTIFICATE/{exit}' /tmp/taiko-cert.pem > /app/Certificates/cert.crt \
    && awk 'BEGIN{c=0} /BEGIN.*PRIVATE KEY/{c=1} c{print} /END.*PRIVATE KEY/{exit}' /tmp/taiko-cert.pem > /app/Certificates/cert.key \
    && rm /tmp/taiko-cert.pem
COPY docker/start.sh /usr/local/bin/start-taiko-green-server
RUN chmod +x /usr/local/bin/start-taiko-green-server

WORKDIR /app
EXPOSE 80 10122 54430 54431
ENTRYPOINT ["start-taiko-green-server"]
