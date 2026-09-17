interface UseCaseSectionProps {
  title: string
  body: string
}

export default function UseCaseSection({ title, body }: UseCaseSectionProps) {
  return (
    <div className="border-b border-paper-border py-20 text-center last:border-none">
      <h2 className="fluid-heading-md mb-4">{title}</h2>
      <p className="mx-auto max-w-lg text-base leading-relaxed text-neutral-600">{body}</p>
    </div>
  )
}
