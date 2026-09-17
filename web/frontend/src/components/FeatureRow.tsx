interface FeatureRowProps {
  title: string
  body: string
  imageLeft?: boolean
}

export default function FeatureRow({ title, body, imageLeft }: FeatureRowProps) {
  const image = (
    <div className="flex min-h-[280px] items-center justify-center rounded-sm bg-paper-dark">
      <span className="text-sm text-neutral-400">Device image</span>
    </div>
  )

  const text = (
    <div>
      <h3 className="fluid-heading-md mb-4">{title}</h3>
      <p className="text-base leading-relaxed text-neutral-600">{body}</p>
    </div>
  )

  return (
    <div className="grid items-center gap-12 md:grid-cols-2">
      {imageLeft ? image : text}
      {imageLeft ? text : image}
    </div>
  )
}
