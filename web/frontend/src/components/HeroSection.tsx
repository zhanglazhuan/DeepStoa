import { Link } from 'react-router-dom'

export default function HeroSection() {
  return (
    <section className="flex min-h-[90vh] items-center justify-center bg-gradient-to-b from-paper to-paper-dark px-6 py-24 text-center lg:px-12">
      <div className="max-w-3xl">
        <h1 className="fluid-heading-xl mb-6">
          The Focused Learning Device
        </h1>
        <p className="mx-auto mb-10 max-w-xl text-lg text-neutral-600">
          Distraction-free education. Paper-like reading, flashcard learning,
          and time management — in a device the size of a phone.
        </p>
        <Link
          to="/pre-order"
          className="inline-block rounded-full bg-neutral-900 px-8 py-4 text-base font-medium text-white transition-colors hover:bg-neutral-700"
        >
          Pre-order now
        </Link>
      </div>
    </section>
  )
}
