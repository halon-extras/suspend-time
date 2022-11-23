# Suspend by time

Suspend delivery at certain times, the format is in "crontime". If the crontime matches (that particual minute) it will or will not suspend the queue. This can be used eg. to suspend traffic on evening or weekends by regulatory law.

## Installation

Follow the [instructions](https://docs.halon.io/manual/comp_install.html#installation) in our manual to add our package repository and then run the below command.

### Ubuntu

```
apt-get install halon-extras-suspend-time
```

### RHEL

```
yum install halon-extras-suspend-time
```

## Configuration example

Only deliver mail on working hours on weekdays

### smtpd-app.yaml

```
plugins:
  - id: suspend-time
    config:
      suspends:
        - tag: nonworkhours
          ifnot:
            - "* 8-17 * * 1-5"
```
