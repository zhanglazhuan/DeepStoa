const specs = [
  ['Screen', '3.97" E-ink, 800×480, dual-point touch'],
  ['Processor', 'ESP32S3'],
  ['Operating System', 'Zephyr RTOS'],
  ['Connectivity', 'Wi-Fi 802.11 b/g/n, Bluetooth LE 5.0'],
  ['Battery', 'Weeks on a single charge'],
  ['Storage', '16MB flash'],
  ['Charging', 'USB-C'],
]

export default function SpecsTable() {
  return (
    <div>
      <h3 className="fluid-heading-md mb-10 text-center">Technical specifications</h3>
      <div className="mx-auto max-w-2xl">
        {specs.map(([label, value]) => (
          <div key={label} className="flex border-b border-paper-border py-4 text-sm">
            <dt className="w-40 shrink-0 text-neutral-400">{label}</dt>
            <dd className="text-neutral-900">{value}</dd>
          </div>
        ))}
      </div>
    </div>
  )
}
