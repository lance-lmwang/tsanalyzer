# Kubernetes Best Practices: TsAnalyzer in the Cloud

Deploying TsAnalyzer Pro in Kubernetes requires specialized networking and scheduling configurations to unlock the raw performance of the deterministic C-core data plane.

---

## 1. Networking Strategy

Standard Kubernetes ClusterIP and Service VIPs introduce significant jitter and software-interrupt overhead. For 10Gbps+ monitoring, use one of the following:

### 1.1 HostNetwork (Simplest)
Bypasses the K8s network stack and CNI entirely.
*   **Pros**: Zero overhead, direct access to NIC hardware timestamps.
*   **Cons**: Port conflicts on the same node.
*   **YAML Snippet**:
```yaml
spec:
  template:
    spec:
      hostNetwork: true
      containers:
      - name: tsanalyzer
        image: tsanalyzer:latest
```

### 1.2 Multus CNI (Recommended for Multi-tenant)
Attaches a secondary "Data Plane" interface (e.g., via SR-IOV or Macvlan) to the pod.
*   **Pros**: Isolation between Control (eth0) and Data (net1) planes.
*   **Requirements**: Multus CNI plugin installed.

---

## 2. Resource & Scheduling

### 2.1 CPU Pinning (Guaranteed QoS)
TsAnalyzer's lock-free queues rely on predictable thread scheduling. Always use `Guaranteed` QoS class.
```yaml
resources:
  requests:
    cpu: "4"
    memory: "4Gi"
  limits:
    cpu: "4"
    memory: "4Gi"
```

### 2.2 Hugepages
To reduce TLB misses during high-density stream ingestion, enable 2MB Hugepages.
```yaml
volumeMounts:
- mountPath: /dev/hugepages
  name: hugepage
volumes:
- name: hugepage
  emptyDir:
    medium: HugePages
```

---

## 3. High Availability (HA)

### 3.1 Leader Election
When running multiple replicas, use the `Distributed Configuration Center` (Etcd) to ensure only one node owns the analysis of a specific multicast group to prevent duplicate metrics.

### 3.2 Liveness Probe
Use the `/health` endpoint provided by the TsAnalyzer REST API.
```yaml
livenessProbe:
  httpGet:
    path: /health
    port: 8080
  initialDelaySeconds: 5
  periodSeconds: 10
```
