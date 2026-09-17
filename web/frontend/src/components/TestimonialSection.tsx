const testimonials = [
  {
    quote: "DeepStoa helped my daughter build a daily reading habit. The e-ink screen means she reads before bed without blue light exposure.",
    name: 'Sarah Chen',
    role: 'Parent of 3rd grader',
  },
  {
    quote: "I use DeepStoa to prep lesson plans and share flashcard decks with my students. It's become an essential part of my classroom.",
    name: 'James Liu',
    role: 'Middle school teacher',
  },
  {
    quote: "Finally, a device that doesn't try to distract me. My flashcard review sessions are twice as productive now.",
    name: 'Maya Patel',
    role: 'High school student',
  },
]

export default function TestimonialSection() {
  return (
    <section className="px-6 py-36 lg:px-12">
      <div className="mx-auto max-w-7xl">
        <h2 className="fluid-heading-lg mb-20 text-center">
          Stories from our community
        </h2>
        <div className="grid gap-12 md:grid-cols-3">
          {testimonials.map((t) => (
            <blockquote key={t.name} className="flex flex-col rounded-2xl bg-neutral-100 p-8">
              <p className="mb-6 flex-1 text-base leading-relaxed text-neutral-700">
                &ldquo;{t.quote}&rdquo;
              </p>
              <footer>
                <cite className="block text-sm font-medium not-italic text-neutral-900">
                  {t.name}
                </cite>
                <span className="text-sm text-neutral-500">{t.role}</span>
              </footer>
            </blockquote>
          ))}
        </div>
      </div>
    </section>
  )
}
