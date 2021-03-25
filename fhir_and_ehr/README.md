# FHIR and EHR

- [FHIR endpoint setup](#fhir-endpoint-setup)
- [EHR](#ehr)
- [Demo](#demo)
- [References](#references)

## FHIR endpoint setup

1. Go to digitalocean.com and create a droplet.
    ```
    Ubuntu 20.04
    SHARED CPU
    Regular Intel with SSD
    4 GB / 2 CPUs
    80 GB SSD Disk
    4 TB transfer
    Singapore
    ```

2. Buy a domain for easy to remember, in my case: chris.school

3. In your computer, open terminal and connect to the droplet:

    ```
    ssh root@178.128.90.168
    ```


4. Start to setup in droplet.

    ```
    sudo apt update
    sudo apt install apt-transport-https ca-certificates curl software-properties-common
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
    sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu focal stable"
    sudo apt update
    apt-cache policy docker-ce
    sudo apt install docker-ce
    sudo systemctl status docker

    sudo usermod -aG docker ${USER}
    su - ${USER}
    id -nG

    sudo curl -L "https://github.com/docker/compose/releases/download/1.27.4/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
    sudo chmod +x /usr/local/bin/docker-compose
    docker-compose --version

    git clone https://github.com/ChrisSchool/dissertation.git
    cd dissertation/fhir_and_ehr/src
    ```

    modify the host, change to `HOST=fhir.chris.school`
    ```
    nano .env
    ```

    run the docker:

    ```
    docker-compose up -d
    docker-compose ps
    ```

    finally it output the details

    ```
        Name                    Command               State           Ports         
    ---------------------------------------------------------------------------------
    fhir-viewer       nginx -g daemon off;             Up      0.0.0.0:4011->80/tcp  
    hapi-r2           sh -c  envsubst < /tmp/hap ...   Up      0.0.0.0:4002->8080/tcp
    hapi-r3           sh -c  envsubst < /tmp/hap ...   Up      0.0.0.0:4003->8080/tcp
    hapi-r4           sh -c  envsubst < /tmp/hap ...   Up      0.0.0.0:4004->8080/tcp
    home-page         /docker-entrypoint.sh sh - ...   Up      0.0.0.0:4000->80/tcp  
    launcher          docker-entrypoint.sh node  ...   Up      0.0.0.0:4013->80/tcp  
    patient-browser   sh -c  envsubst < /usr/sha ...   Up      0.0.0.0:4012->80/tcp
    ```

5. Open http://fhir.chris.school:4000 in your browser to access.

## EHR

This project has already generated multiple records of synthetic realistic data. Synthea is a Synthetic Patient Population Simulator. The goal is to output synthetic, realistic (but not real), patient data and associated health records in a variety of formats.

check here to see the 629 sample patients data:
> http://fhir.chris.school:4004/hapi-fhir-jpaserver/fhir/Patient

## Demo

### Deployment

I have deployed to my domain. http://fhir.chris.school:4000

- Homepage

    http://fhir.chris.school:4000

- FHIR R4 Server

    - Browser Sample Patients

        http://fhir.chris.school:4012/index.html?config=r4-local#/

    - Open HAPI Server UI

        http://fhir.chris.school:4004/hapi-fhir-jpaserver/

### Query resources by CURL

Query the patient data (id: 1)

```
curl http://fhir.chris.school:4004/hapi-fhir-jpaserver/fhir/Patient/1/_history/1?_pretty=true&_format=json
```

Query "Body Height" observations (code: `http://loinc.org|8302-2`)

```
curl http://fhir.chris.school:4004/hapi-fhir-jpaserver/fhir/Observation?_pretty=true&code=http%3A%2F%2Floinc.org%7C8302-2&_format=json
```

For other FHIR API usages, please check the official FHIR documents.

## References

- https://github.com/smart-on-fhir/smart-dev-sandbox
- https://www.digitalocean.com/community/tutorials/how-to-install-and-use-docker-on-ubuntu-20-04
- https://www.digitalocean.com/community/tutorials/how-to-install-and-use-docker-compose-on-ubuntu-20-04
- https://github.com/synthetichealth/synthea
