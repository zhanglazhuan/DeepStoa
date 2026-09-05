import UseCaseSection from '../components/UseCaseSection'
import Footer from '../components/Footer'

const useCases = [
  {
    title: 'Reading',
    body: 'Load textbooks, novels, and articles. E-ink reads like paper — no eye fatigue even after hours. Adjustable fonts and sizes for every reader. Highlight, annotate, and bookmark without distraction.',
  },
  {
    title: 'Flashcard Learning',
    body: 'Create decks or import shared ones. Built-in spaced repetition helps students retain knowledge longer. The tactile touch screen makes flipping cards feel natural and satisfying.',
  },
  {
    title: 'Time Management',
    body: 'Pomodoro timer, study planner, and schedule view. Helps students build focus habits without a smartphone nearby. Set goals, track progress, and celebrate streaks.',
  },
]

export default function UseCasesPage() {
  return (
    <>
      <section className="px-6 py-32 text-center lg:px-12">
        <p className="mb-6 text-xs font-medium uppercase tracking-[0.2em] text-neutral-400">
          What DeepStoa can do
        </p>
        <h1 className="fluid-heading-xl">Three tools. One device.</h1>
      </section>

      <section className="mx-auto max-w-3xl px-6 pb-32 lg:px-12">
        {useCases.map((uc) => (
          <UseCaseSection key={uc.title} title={uc.title} body={uc.body} />
        ))}
      </section>

      <Footer />
    </>
  )
}
