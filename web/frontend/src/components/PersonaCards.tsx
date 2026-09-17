const personas = [
  {
    title: 'Students',
    body: 'Read textbooks, review flashcards, and manage study schedules on one e-ink device. No notifications, no games, no distractions.',
  },
  {
    title: 'Teachers',
    body: 'Create and distribute learning materials. Share flashcard decks with your class and track engagement.',
  },
  {
    title: 'Parents',
    body: 'Give your child a screen that won\'t strain their eyes or distract them from what matters most: learning.',
  },
]

export default function PersonaCards() {
  return (
    <section className="px-6 py-24 lg:px-12">
      <div className="mx-auto max-w-7xl">
        <p className="mb-16 text-center text-xs font-medium uppercase tracking-[0.2em] text-neutral-400">
          Who is DeepStoa for?
        </p>
        <div className="flex gap-6 overflow-x-auto pb-4 md:grid md:grid-cols-3 md:overflow-visible">
          {personas.map((p) => (
            <div
              key={p.title}
              className="min-w-[280px] rounded-sm bg-white px-8 py-10 shadow-sm md:min-w-0"
            >
              <h3 className="mb-3 text-xl font-medium text-neutral-900">{p.title}</h3>
              <p className="text-sm leading-relaxed text-neutral-600">{p.body}</p>
            </div>
          ))}
        </div>
      </div>
    </section>
  )
}
