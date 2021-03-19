## Suspend by time

Suspend delivery at certain times, the format is in "crontime". If the crontime matches (that particual minute) it will or will not suspend the queue. This can be used eg. to suspend traffic on evening or weekends by regulatory law.

### Examples

Only deliver mail on working hours on weekdays

```
suspends:
 - tag: nonworkhours
   ifnot:
    - "* 8-17 * * 1-5"
```
