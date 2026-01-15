const isValidMqttHost = (host: string) => {
    const value = host.trim();
    if (!value) return false;

    // 支持 IPv4
    const ipv4 = /^(25[0-5]|2[0-4]\d|1?\d{1,2})(\.(25[0-5]|2[0-4]\d|1?\d{1,2})){3}$/;

    // 支持域名（允许中划线，段长 1-63）
    const hostname =        /^(?!-)[A-Za-z0-9-]{1,63}(?:\.(?!-)[A-Za-z0-9-]{1,63})*$/;

    return ipv4.test(value) || hostname.test(value);
};

const isValidHostname = (hostname: string) => {
    const value = hostname.trim();
    if (!value) return false;
    const hostnameRegex = /^(?!-)[A-Za-z0-9-]{1,63}(?:\.(?!-)[A-Za-z0-9-]{1,63})*$/;
    return hostnameRegex.test(value);
};

const isValidPoeIp = (ip: string) => {
    const value = ip.trim();
    if (!value) return false;
    const ipv4 = /^(25[0-5]|2[0-4]\d|1?\d{1,2})(\.(25[0-5]|2[0-4]\d|1?\d{1,2})){3}$/;
    return ipv4.test(value);
};

export { isValidMqttHost, isValidPoeIp, isValidHostname };