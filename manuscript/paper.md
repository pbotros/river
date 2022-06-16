---
title: 'River: a cross-platform, high-throughput, structured streaming framework'
tags:
  - neuroscience
  - closed-loop
  - real-time
  - streaming
authors:
  - name: Paul Botros
    orcid: 0000-0002-4797-1651
    affiliation: 1 # (Multiple affiliations must be quoted)
  - name: Jose M. Carmena
    affiliation: "1, 2"
affiliations:
 - name: Department of Electrical Engineering and Computer Sciences, University of California Berkeley, Berkeley, CA 94720, United States of America
   index: 1
 - name: Helen Wills Neuroscience Institute, University of California Berkeley, Berkeley, CA 94720, United States of America
   index: 2
date: 16 June 2022
bibliography: paper.bib

---

# Summary

Modern experiments in neuroscience demand increasingly complex and performant systems. Technological advances in the past decades have led to an explosion in neural data volume, with current technologies capable of simultaneous recordings of thousands of individual neurons [@Ota; @Steinmetz]. Concomitantly, to improve scientific efficiency [@Chen] or modulate neural or physical systems [@Shanechi], closed-loop experimental paradigms that multiplex neural and non-neural (e.g. kinematic) data have come to the forefront of research, where real-time computations on various data streams directly influence the experiment. Considering these ever-increasing demands on both latency and throughput of data, there is a dire need in the field to stream high-throughput data between devices, balancing requirements for performance with simplicity for non-software experts.

To tackle these problems, we developed River, a structured streaming solution to streamline data management for modern neuroscience experiments. Producers of data – “writers” – first define a structure for each data sample for a particular stream. Then, utilizing function calls similar to those of file I/O, writers can then write data to that stream, while an unlimited number of “readers” can consume this data in real-time by specifying the appropriate stream name. Each reader can read at its own pace, with support for blocking for a given period of time or number of samples. Readers and writers only require network connectivity to a centralized server, enabling a horizontal scaling paradigm useful for computationally-intensive or cloud-reliant experiments. Finally, leveraging the structured nature of each stream, a separate “ingester” process consumes each stream’s data and writes data to disk in a cross-platform tabular format, [Parquet](https://github.com/apache/parquet-format), for post-hoc analysis. Once persisted by the ingester, sufficiently stale data is then deleted from the River stream, enabling streams to be indefinitely long. River is written in C++ with Python and MATLAB bindings and works under Linux, Mac OSX, and Windows.

Eschewing a bespoke, research-specific solution to ease its maintenance burden, we utilized an industry-standard database, [Redis](https://redis.io), to underlie River, enabling sub-millisecond latencies, low jitter, and 20+ MB/s throughput. Critical to researchers, River guarantees that no written data can be dropped by any readers, even in the case of long pauses in reading, and that all persisted data exactly mirrors the data streamed online. These guarantees come in contrast to the best-effort paradigm of another popular research-oriented streaming framework [@labstreaminglayer]. Additionally, timestamp management is centralized and thus stream synchronization is simplified. Other streaming solutions widely used in the software industry, such as [Kafka](https://kafka.apache.org/) and [RabbitMQ](https://www.rabbitmq.com/), have more strict guarantees on data availability and delivery but subsequently suffer in raw streaming performance and can be difficult to set up robustly.

Finally, River has been successfully used in two closed-loop neuroscience studies by the author [@Formento] \(Botros et. al in preparation\). While River was designed with neuroscience in mind, we believe it can have applications in other fields that might have similar data management needs.

# References
