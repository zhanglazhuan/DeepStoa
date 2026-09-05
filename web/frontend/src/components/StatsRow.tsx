const stats = [
  ['2024', 'Founded'],
  ['50+', 'Schools piloting'],
  ['3.97"', 'Perfect size'],
  ['0', 'Distractions'],
]

export default function StatsRow() {
  return (
    <div className="grid grid-cols-2 gap-8 md:grid-cols-4">
      {stats.map(([value, label]) => (
        <div key={label} className="text-center">
          <p className="fluid-heading-md mb-1">{value}</p>
          <p className="text-sm text-neutral-500">{label}</p>
        </div>
      ))}
    </div>
  )
}
