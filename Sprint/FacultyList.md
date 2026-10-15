# BU Faculty Relevant to My UWB TDoA Real-time Localization

I selected three professors whose work touches localization reliability, safety under uncertain measurements, and practical evaluation of autonomous systems. I checked BU faculty profiles and lab or publication pages on October 12, 2026. The course Research Areas handout was not available in this project folder, so I used BU's official [Intelligent Autonomy and Robotics research-area page](https://www.bu.edu/eng/academics/departments-and-divisions/systems-engineering/se-research/intelligent-autonomy-and-robotics-2/) and individual faculty profiles as starting points.

## 1. Wenchao Li — Safety Under Uncertain or Altered Data

**BU affiliation:** Associate Professor, Electrical and Computer Engineering and Systems Engineering. [Official faculty profile](https://www.bu.edu/eng/profile/wenchao-li-ph-d/).

**Paper checked:** H. M. Sabbir Ahmad, Ehsan Sabouni, Wei Xiao, Christos G. Cassandras, and Wenchao Li, [Safe and Secure Control of Connected and Automated Vehicles: An Event-Triggered Control Approach using Trust-Aware Robust Control Barrier Functions](https://dspace.mit.edu/entities/publication/87a9a5d7-0e81-4b65-9636-0fe04cbcda9e), *ACM Transactions on Cyber-Physical Systems*, 2025. The abstract addresses communication attacks, measurement uncertainty, and trust-aware safe coordination. I also checked the Dependable Computing Laboratory's [publication list](https://sites.bu.edu/depend/publications/).

**Why relevant:** My system uses uncertain, delayed position data to raise zone-entry and abnormal-motion alerts. Li's research is relevant to defining what the software should do when a measurement is unreliable. His paper concerns vehicle control, while my prototype is a monitoring demonstration; its safety guarantees do not transfer automatically.

**One specific question I would ask:** “For my restricted-zone alert demo, should I test a warning margin based on measured position error plus maximum robot speed times tracking delay, rather than waiting for two consecutive estimated positions inside the zone, to reduce missed entries?”

## 2. Eshed Ohn-Bar — Practical Evaluation of Safety Alerts

**BU affiliation:** Assistant Professor, Electrical and Computer Engineering. [Official faculty profile](https://www.bu.edu/eng/profile/ohn-bar-eshed/).

**Paper checked:** Animikh Aich, Adwait Kulkarni, and Eshed Ohn-Bar, [Scalable Offline Metrics for Autonomous Driving](https://arxiv.org/abs/2510.08571), 2025. The paper investigates how offline prediction-error metrics relate to actual driving behavior and explores an uncertainty-based metric. I checked its abstract and his [Human-to-Everything Lab page](https://eshed1.github.io/), which describes research in robust autonomy, real-time assistance, and human-machine interaction.

**Why relevant:** My evaluation needs to establish whether acceptable coordinate accuracy also produces reliable, timely safety-zone alerts. Ohn-Bar's work provides a useful connection between numerical prediction error and behavior during operation. His paper concerns autonomous driving rather than UWB, so I will use it to guide scenario selection and evaluation rather than assume its results transfer to my hardware.

**One specific question I would ask:** “For my UWB safety-zone monitoring prototype, which failure scenarios should I prioritize to show whether acceptable position accuracy actually translates into reliable entry alerts—for example, fast boundary crossings, delayed updates, or temporary signal loss?”
